#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <utility>

namespace {

struct AllocatorStats {
    std::size_t allocationCalls = 0;
    std::size_t freeCalls = 0;
    std::size_t activeAllocations = 0;
};

void *tracking_allocate(void *userData, void *pointer, std::size_t,
                        std::size_t newSize) {
    auto &stats = *static_cast<AllocatorStats *>(userData);

    if (newSize == 0) {
        if (pointer != nullptr) {
            ++stats.freeCalls;
            --stats.activeAllocations;
        }
        std::free(pointer);
        return nullptr;
    }

    void *result = std::realloc(pointer, newSize);
    if (result != nullptr) {
        ++stats.allocationCalls;
        if (pointer == nullptr) {
            ++stats.activeAllocations;
        }
    }

    return result;
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
