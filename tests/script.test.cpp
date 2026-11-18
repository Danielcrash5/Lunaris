#include <gtest/gtest.h>
#include <lunaris/state.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using lunaris::Error;
using lunaris::ErrorKind;
using lunaris::ScriptResult;
using lunaris::State;
using lunaris::Type;
} // namespace

template <typename T>
concept CanGet = requires(const ScriptResult &result) { result.get<T>(0); };

static_assert(CanGet<bool>);
static_assert(CanGet<double>);
static_assert(CanGet<std::string>);
static_assert(!CanGet<int>);

TEST(ScriptTest, ReturnsValuesInOrder) {
    State state;

    const auto result = state.script(
        "return nil, true, 42, 'hello', {}, function() end", "values");

    ASSERT_EQ(result.size(), 6U);
    EXPECT_EQ(result.type(0), Type::nil);
    EXPECT_EQ(result.type(1), Type::boolean);
    EXPECT_EQ(result.type(2), Type::number);
    EXPECT_EQ(result.type(3), Type::string);
    EXPECT_EQ(result.type(4), Type::table);
    EXPECT_EQ(result.type(5), Type::function);
}

TEST(ScriptTest, SupportsAViewIntoNonNullTerminatedSource) {
    State state;
    constexpr std::string_view source = "return 7;discarded text";

    const auto result = state.script(source.substr(0, source.find(';')));

    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.type(0), Type::number);
}

TEST(ScriptTest, ScriptCanReturnNoValues) {
    State state;

    const auto result = state.script("local value = 1");

    EXPECT_EQ(result.size(), 0U);
}

TEST(ScriptTest, TypeRejectsAnIndexOutsideTheResult) {
    State state;
    const auto result = state.script("return 1");

    EXPECT_THROW(result.type(1), std::out_of_range);
}

TEST(ScriptResultTest, GetsSupportedValues) {
    State state;
    const auto result = state.script("return true, 4.5, 'hello'");

    ASSERT_EQ(result.size(), 3U);
    EXPECT_TRUE(result.get<bool>(0));
    EXPECT_DOUBLE_EQ(result.get<double>(1), 4.5);
    EXPECT_EQ(result.get<std::string>(2), "hello");
}

TEST(ScriptResultTest, GetRejectsAValueOfTheWrongType) {
    State state;
    const auto result = state.script("return 'hello'");

    try {
        (void)result.get<double>(0);
        FAIL() << "get<double>() did not report the type mismatch";
    } catch (const Error &error) {
        EXPECT_EQ(error.kind(), ErrorKind::type);
    }
}

TEST(ScriptResultTest, GetRejectsAnIndexOutsideTheResult) {
    State state;
    const auto result = state.script("return 1.0");

    EXPECT_THROW(result.get<double>(1), std::out_of_range);
}

TEST(ScriptTest, CompileFailureHasCompileDetails) {
    State state;

    try {
        (void)state.script("local =", "bad_syntax");
        FAIL() << "script() did not report the compile error";
    } catch (const Error &error) {
        EXPECT_EQ(error.kind(), ErrorKind::compile);
        EXPECT_EQ(error.chunkName(), "bad_syntax");
        EXPECT_FALSE(std::string_view(error.what()).empty());
    }
}

TEST(ScriptTest, RuntimeFailureHasRuntimeDetails) {
    State state;
    state.openLibraries(lunaris::Library::base);

    try {
        (void)state.script("error('boom')", "runtime_case");
        FAIL() << "script() did not report the runtime error";
    } catch (const Error &error) {
        EXPECT_EQ(error.kind(), ErrorKind::runtime);
        EXPECT_EQ(error.chunkName(), "runtime_case");
        ASSERT_TRUE(error.lineNumber().has_value());
        EXPECT_EQ(*error.lineNumber(), 1U);
        EXPECT_FALSE(error.traceback().empty());
    }
}

TEST(ScriptTest, SafeScriptReturnsResultsOnSuccess) {
    State state;

    auto result = state.safeScript("return 1, 'two'");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ(result->type(0), Type::number);
    EXPECT_EQ(result->type(1), Type::string);
}

TEST(ScriptTest, SafeScriptReturnsCompileErrorOnFailure) {
    State state;

    auto result = state.safeScript("return )", "safe_bad_syntax");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind(), ErrorKind::compile);
    EXPECT_EQ(result.error().chunkName(), "safe_bad_syntax");
}

TEST(ScriptTest, SafeScriptReturnsRuntimeErrorOnFailure) {
    State state;
    state.openLibraries(lunaris::Library::base);

    auto result = state.safeScript("error('boom')", "safe_runtime_case");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind(), ErrorKind::runtime);
    EXPECT_EQ(result.error().chunkName(), "safe_runtime_case");
    EXPECT_EQ(result.error().lineNumber(), 1U);
    EXPECT_FALSE(result.error().traceback().empty());
}

TEST(ScriptTest, ResultReportsClosedStateAndCanStillBeDestroyed) {
    lunaris::ScriptResult result = [&] {
        State state;
        return state.script("return 1");
    }();

    EXPECT_EQ(result.size(), 1U);
    try {
        (void)result.type(0);
        FAIL() << "type() did not report the closed State";
    } catch (const Error &error) {
        EXPECT_EQ(error.kind(), ErrorKind::state);
    }

    try {
        (void)result.get<double>(0);
        FAIL() << "get() did not report the closed State";
    } catch (const Error &error) {
        EXPECT_EQ(error.kind(), ErrorKind::state);
    }

    // Destruction after the State has closed is part of ScriptResult's contract.
}
