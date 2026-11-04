#include "lunaris/State.h"

#include <gtest/gtest.h>
#include <lua.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

TEST(ScriptTests, CompilesAndExecutesReturningScalarValues) {
    lunaris::State state;
    ASSERT_TRUE(state.set_global("integer", lunaris::Value{std::int64_t{42}}));
    auto loaded =
        state.load_script("return integer, 42, true, 3.5, 'done', nil",
                          "game://scripts/result.luau");

    ASSERT_TRUE(loaded);
    auto result = loaded.value().execute();

    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 6U);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[0]), 42);
    EXPECT_EQ(std::get<double>(result.value()[1]), 42.0);
    EXPECT_TRUE(std::get<bool>(result.value()[2]));
    EXPECT_DOUBLE_EQ(std::get<double>(result.value()[3]), 3.5);
    EXPECT_EQ(std::get<std::string>(result.value()[4]), "done");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result.value()[5]));
    EXPECT_EQ(state.top(), 0);
}

TEST(ScriptTests, LoadsSyntaxErrorsAsCompileErrors) {
    lunaris::State state;
    auto result = state.load_script("local = ", "broken.luau");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Compile);
    EXPECT_FALSE(result.error().message().empty());
    EXPECT_EQ(result.error().source(), "broken.luau");
    EXPECT_EQ(result.error().line(), 1);
    EXPECT_GE(result.error().column(), 1);
    EXPECT_EQ(state.top(), 0);
}

TEST(ScriptTests, ReturnsRuntimeErrorsAndRestoresStack) {
    lunaris::State state;
    state.push(7);
    auto loaded = state.load_script("local value = nil; return value.member",
                                    "game://scripts/failure.luau");
    ASSERT_TRUE(loaded);

    auto result = loaded.value().execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_FALSE(result.error().message().empty());
    EXPECT_EQ(result.error().source(), "game://scripts/failure.luau");
    EXPECT_EQ(state.top(), 1);
    EXPECT_EQ(state.get<int>(-1), 7);
}

TEST(ScriptTests, ReportsUnsupportedReferenceReturnValues) {
    lunaris::State state;
    auto loaded = state.load_script("return {}", "table-result.luau");
    ASSERT_TRUE(loaded);

    auto result = loaded.value().execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Type);
    EXPECT_EQ(state.top(), 0);
}

TEST(ScriptTests, ReusesCompiledChunkAcrossExecutions) {
    lunaris::State state;
    ASSERT_TRUE(state.set_global("counter", lunaris::Value{0.0}));
    auto loaded = state.load_script("counter = counter + 1; return counter",
                                    "counter.luau");
    ASSERT_TRUE(loaded);

    auto first = loaded.value().execute();
    auto second = loaded.value().execute();

    ASSERT_TRUE(first) << first.error().message();
    ASSERT_TRUE(second) << second.error().message();
    EXPECT_EQ(std::get<double>(first.value()[0]), 1.0);
    EXPECT_EQ(std::get<double>(second.value()[0]), 2.0);
}

TEST(ScriptTests, RejectsChunkNamesContainingEmbeddedNull) {
    lunaris::State state;
    const std::string chunkName("script\0name", 11);

    auto result = state.load_script("return true", chunkName);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Load);
    EXPECT_EQ(state.top(), 0);
}

TEST(ScriptTests, ScriptHandleRemainsValidWhenStateIsMoved) {
    lunaris::State original;
    auto loaded = original.load_script("return 99", "move.luau");
    ASSERT_TRUE(loaded);
    lunaris::Script script = std::move(loaded).value();

    lunaris::State moved(std::move(original));
    auto result = script.execute();

    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<double>(result.value()[0]), 99.0);
    EXPECT_EQ(moved.top(), 0);
}

TEST(ScriptTests, HandleFailsSafelyAfterStateDestruction) {
    std::optional<lunaris::Script> script;
    {
        lunaris::State state;
        auto loaded = state.load_script("return 1", "expired.luau");
        ASSERT_TRUE(loaded);
        script.emplace(std::move(loaded).value());
    }

    EXPECT_THROW((void)script->execute(), std::logic_error);
    script.reset();
}
