#include "lunaris/State.h"

#include <gtest/gtest.h>
#include <lua.h>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMemoryLimit = 8 * 1024 * 1024;
constexpr std::size_t kLargeAllocation = 32 * 1024 * 1024;

struct AllocatorStats {
    std::size_t allocationCalls = 0;
    std::size_t freeCalls = 0;
    std::size_t activeAllocations = 0;
    std::size_t allocatedBytes = 0;
    std::size_t peakAllocatedBytes = 0;
};

void *tracking_allocate(void *userData, void *pointer, std::size_t oldSize,
                        std::size_t newSize) {
    auto &stats = *static_cast<AllocatorStats *>(userData);

    if (newSize == 0) {
        if (pointer != nullptr) {
            ++stats.freeCalls;
            --stats.activeAllocations;
            stats.allocatedBytes -= oldSize;
        }
        std::free(pointer);
        return nullptr;
    }

    const bool isNewAllocation = pointer == nullptr;
    void *result = std::realloc(pointer, newSize);
    if (result != nullptr) {
        ++stats.allocationCalls;
        if (isNewAllocation) {
            ++stats.activeAllocations;
        }
        stats.allocatedBytes =
            stats.allocatedBytes - (isNewAllocation ? 0 : oldSize) + newSize;
        stats.peakAllocatedBytes =
            std::max(stats.peakAllocatedBytes, stats.allocatedBytes);
    }

    return result;
}

struct LargeAllocation {
    const char *data;
    std::size_t size;
};

int push_large_string(lua_State *state) {
    auto *allocation = static_cast<LargeAllocation *>(
        lua_touserdata(state, lua_upvalueindex(1)));
    lua_pushlstring(state, allocation->data, allocation->size);
    return 1;
}

} // namespace

TEST(StateTests, CreatesAndOwnsLuauState) {
    lunaris::State state;

    EXPECT_NE(state.native_handle(), nullptr);
}

TEST(StateTests, UsesConfiguredAllocatorUntilStateDestruction) {
    AllocatorStats stats;
    lunaris::StateConfig config;
    config.allocator = {&tracking_allocate, &stats};

    {
        lunaris::State state(config);
        EXPECT_NE(state.native_handle(), nullptr);
        EXPECT_GT(stats.allocationCalls, 0U);
        EXPECT_GT(stats.activeAllocations, 0U);
    }

    EXPECT_EQ(stats.activeAllocations, 0U);
    EXPECT_GT(stats.freeCalls, 0U);
}

TEST(StateTests, MoveTransfersStateOwnership) {
    lunaris::State original;
    auto *const handle = original.native_handle();

    lunaris::State moved(std::move(original));

    EXPECT_EQ(original.native_handle(), nullptr);
    EXPECT_EQ(moved.native_handle(), handle);

    lunaris::State destination;
    destination = std::move(moved);

    EXPECT_EQ(moved.native_handle(), nullptr);
    EXPECT_EQ(destination.native_handle(), handle);
}

TEST(StateTests, MovePreservesAllocatorContext) {
    AllocatorStats stats;
    lunaris::StateConfig config;
    config.allocator = {&tracking_allocate, &stats};
    config.memoryLimit = kMemoryLimit;

    {
        lunaris::State original(config);
        lunaris::State moved(std::move(original));
        lunaris::State destination;
        destination = std::move(moved);

        std::vector<char> data(256 * 1024, 'x');
        lua_pushlstring(destination.native_handle(), data.data(), data.size());
        EXPECT_EQ(lua_type(destination.native_handle(), -1), LUA_TSTRING);
    }

    EXPECT_EQ(stats.allocatedBytes, 0U);
}

TEST(StateTests, RejectsStateWhenLimitCannotCoverInitialization) {
    lunaris::StateConfig config;
    config.memoryLimit = 0;

    EXPECT_THROW(lunaris::State state(config), std::bad_alloc);
}

TEST(StateTests, EnforcesMemoryLimitWithDefaultAllocator) {
    lunaris::StateConfig config;
    config.memoryLimit = kMemoryLimit;
    lunaris::State state(config);

    std::vector<char> data(kLargeAllocation, 'x');
    LargeAllocation allocation{data.data(), data.size()};
    lua_pushlightuserdata(state.native_handle(), &allocation);
    lua_pushcclosure(state.native_handle(), &push_large_string,
                     "push_large_string", 1);

    EXPECT_EQ(lua_pcall(state.native_handle(), 0, 1, 0), LUA_ERRMEM);

    lua_settop(state.native_handle(), 0);
    lua_pushinteger(state.native_handle(), 42);
    EXPECT_EQ(lua_tointeger(state.native_handle(), -1), 42);
}

TEST(StateTests, EnforcesMemoryLimitWithConfiguredAllocator) {
    AllocatorStats stats;
    lunaris::StateConfig config;
    config.allocator = {&tracking_allocate, &stats};
    config.memoryLimit = kMemoryLimit;
    {
        lunaris::State state(config);

        void *allocatorUserData = nullptr;
        const lua_Alloc allocate =
            lua_getallocf(state.native_handle(), &allocatorUserData);
        void *block = allocate(allocatorUserData, nullptr, 0, 1024);
        ASSERT_NE(block, nullptr);
        static_cast<char *>(block)[0] = 'x';

        const std::size_t allocationCallsBeforeRejection =
            stats.allocationCalls;
        EXPECT_EQ(allocate(allocatorUserData, block, 1024, kMemoryLimit),
                  nullptr);
        EXPECT_EQ(stats.allocationCalls, allocationCallsBeforeRejection);
        EXPECT_EQ(static_cast<char *>(block)[0], 'x');
        EXPECT_EQ(allocate(allocatorUserData, block, 1024, 0), nullptr);

        std::vector<char> data(kLargeAllocation, 'x');
        LargeAllocation allocation{data.data(), data.size()};
        lua_pushlightuserdata(state.native_handle(), &allocation);
        lua_pushcclosure(state.native_handle(), &push_large_string,
                         "push_large_string", 1);

        EXPECT_EQ(lua_pcall(state.native_handle(), 0, 1, 0), LUA_ERRMEM);
        lua_settop(state.native_handle(), 0);
        lua_pushinteger(state.native_handle(), 42);
        EXPECT_EQ(lua_tointeger(state.native_handle(), -1), 42);
    }

    EXPECT_LE(stats.peakAllocatedBytes, kMemoryLimit);
    EXPECT_EQ(stats.allocatedBytes, 0U);
}
