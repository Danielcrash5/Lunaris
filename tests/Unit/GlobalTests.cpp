#include "lunaris/State.h"

#include <gtest/gtest.h>
#include <lua.h>

#include <cstdint>
#include <string>
#include <variant>

TEST(GlobalTests, SetsAndReadsScalarGlobals) {
    lunaris::State state;

    ASSERT_TRUE(state.set_global("enabled", lunaris::Value{true}));
    ASSERT_TRUE(state.set_global("answer", lunaris::Value{std::int64_t{42}}));
    ASSERT_TRUE(state.set_global("ratio", lunaris::Value{2.5}));
    ASSERT_TRUE(
        state.set_global("name", lunaris::Value{std::string("Lunaris")}));

    EXPECT_TRUE(std::get<bool>(state.get_global("enabled").value()));
    EXPECT_EQ(std::get<std::int64_t>(state.get_global("answer").value()), 42);
    EXPECT_DOUBLE_EQ(std::get<double>(state.get_global("ratio").value()), 2.5);
    EXPECT_EQ(std::get<std::string>(state.get_global("name").value()),
              "Lunaris");
    EXPECT_EQ(state.top(), 0);
}

TEST(GlobalTests, MissingAndNilGlobalsReadAsNil) {
    lunaris::State state;

    auto missing = state.get_global("missing");
    ASSERT_TRUE(missing);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(missing.value()));

    ASSERT_TRUE(state.set_global("temporary", lunaris::Value{std::int64_t{1}}));
    ASSERT_TRUE(
        state.set_global("temporary", lunaris::Value{std::monostate{}}));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        state.get_global("temporary").value()));
}

TEST(GlobalTests, RejectsReferenceGlobals) {
    lunaris::State state;
    lua_createtable(state.native_handle(), 0, 0);
    lua_setglobal(state.native_handle(), "object");

    auto result = state.get_global("object");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Type);
    EXPECT_EQ(result.error().message(), "global value is not a scalar");
    EXPECT_EQ(state.top(), 0);
}

TEST(GlobalTests, RejectsNamesContainingEmbeddedNull) {
    lunaris::State state;
    const std::string invalidName("valid\0ignored", 13);

    auto getResult = state.get_global(invalidName);
    auto setResult = state.set_global(invalidName, lunaris::Value{true});

    ASSERT_FALSE(getResult);
    ASSERT_FALSE(setResult);
    EXPECT_EQ(getResult.error().type(), lunaris::ErrorType::Type);
    EXPECT_EQ(setResult.error().type(), lunaris::ErrorType::Type);
    EXPECT_EQ(state.top(), 0);
}

TEST(GlobalTests, GlobalAccessPreservesExistingStackValues) {
    lunaris::State state;
    state.push(7);

    ASSERT_TRUE(state.set_global("answer", lunaris::Value{std::int64_t{42}}));
    auto result = state.get_global("answer");

    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<std::int64_t>(result.value()), 42);
    EXPECT_EQ(state.top(), 1);
    EXPECT_EQ(state.get<int>(-1), 7);
}
