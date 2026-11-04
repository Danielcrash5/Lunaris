#include "lunaris/State.h"

#include <gtest/gtest.h>
#include <lua.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

TEST(ValueStackTests, PushesAndReadsScalarTypes) {
    lunaris::State state;

    state.push(nullptr);
    state.push(true);
    state.push(-42);
    state.push(3.5);
    state.push(std::string("Luau\0string", 11));

    EXPECT_EQ(state.top(), 5);
    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(state.get<lunaris::Value>(1)));
    EXPECT_TRUE(state.get<bool>(2));
    EXPECT_EQ(state.get<int>(3), -42);
    EXPECT_DOUBLE_EQ(state.get<double>(4), 3.5);
    EXPECT_EQ(state.get<std::string>(5), std::string("Luau\0string", 11));

    state.push("literal");
    EXPECT_EQ(state.get<std::string>(-1), "literal");
    EXPECT_THROW(state.push(static_cast<const char *>(nullptr)),
                 std::invalid_argument);
}

TEST(ValueStackTests, ReadsIntegerAsNumberAndPreservesValueKind) {
    lunaris::State state;
    state.push(std::int64_t{42});

    EXPECT_DOUBLE_EQ(state.get<double>(-1), 42.0);
    EXPECT_TRUE(
        std::holds_alternative<std::int64_t>(state.get<lunaris::Value>(-1)));
}

TEST(ValueStackTests, RejectsIncompatibleTypesAndIndices) {
    lunaris::State state;
    state.push(true);
    state.push(2.5);
    state.push("text");

    EXPECT_THROW((void)state.get<int>(1), std::invalid_argument);
    EXPECT_THROW((void)state.get<int>(2), std::invalid_argument);
    EXPECT_THROW((void)state.get<bool>(2), std::invalid_argument);
    EXPECT_THROW((void)state.get<double>(3), std::invalid_argument);
    EXPECT_THROW((void)state.get<bool>(0), std::out_of_range);
    EXPECT_THROW((void)state.get<bool>(4), std::out_of_range);
    EXPECT_THROW((void)state.get<bool>(-4), std::out_of_range);
}

TEST(ValueStackTests, RejectsIntegersOutsideRequestedRanges) {
    lunaris::State state;
    state.push(std::numeric_limits<std::int64_t>::max());

    EXPECT_THROW((void)state.get<int>(-1), std::out_of_range);
    EXPECT_THROW(state.push(std::numeric_limits<std::uint64_t>::max()),
                 std::out_of_range);
}

TEST(ValueStackTests, PopAndClearMaintainStackHeight) {
    lunaris::State state;
    state.push(1);
    state.push(2);
    state.push(3);

    state.pop(2);
    EXPECT_EQ(state.top(), 1);
    EXPECT_EQ(state.get<int>(-1), 1);

    EXPECT_THROW(state.pop(-1), std::invalid_argument);
    EXPECT_THROW(state.pop(2), std::out_of_range);
    EXPECT_EQ(state.top(), 1);

    state.pop(0);
    EXPECT_EQ(state.top(), 1);
    state.clear();
    EXPECT_EQ(state.top(), 0);
    EXPECT_THROW(state.pop(), std::out_of_range);
}

TEST(ValueStackTests, RejectsReferenceValuesInScalarVariant) {
    lunaris::State state;
    lua_createtable(state.native_handle(), 0, 0);

    EXPECT_THROW((void)state.get<lunaris::Value>(-1), std::invalid_argument);
}

TEST(ValueStackTests, RejectsOperationsOnMovedFromState) {
    lunaris::State original;
    lunaris::State moved(std::move(original));

    EXPECT_THROW((void)original.top(), std::logic_error);
    EXPECT_THROW(original.push(1), std::logic_error);
    EXPECT_EQ(moved.top(), 0);
}
