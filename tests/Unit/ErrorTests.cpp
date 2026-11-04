#include "lunaris/Error.h"
#include "lunaris/Result.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

TEST(ErrorTests, StoresStructuredDiagnostic) {
    const lunaris::Error error(lunaris::ErrorType::Compile, "unexpected token",
                               "game://scripts/main.luau", 12, 4);

    EXPECT_EQ(error.type(), lunaris::ErrorType::Compile);
    EXPECT_EQ(error.message(), "unexpected token");
    EXPECT_EQ(error.source(), "game://scripts/main.luau");
    EXPECT_EQ(error.line(), 12);
    EXPECT_EQ(error.column(), 4);
}

TEST(ErrorTests, MissingLocationUsesSentinelValues) {
    const lunaris::Error error(lunaris::ErrorType::Memory, "out of memory");

    EXPECT_TRUE(error.source().empty());
    EXPECT_EQ(error.line(), -1);
    EXPECT_EQ(error.column(), -1);
}

TEST(ResultTests, StoresAndMovesSuccessfulValues) {
    auto result = lunaris::Result<std::unique_ptr<int>>::success(
        std::make_unique<int>(42));

    ASSERT_TRUE(result);
    EXPECT_EQ(*result.value(), 42);
    EXPECT_EQ(*std::move(result).value(), 42);
}

TEST(ResultTests, StoresStructuredFailure) {
    auto result = lunaris::Result<int>::failure(
        lunaris::Error(lunaris::ErrorType::Runtime, "attempt to call nil",
                       "module.luau", 8, 2));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_EQ(result.error().message(), "attempt to call nil");
    EXPECT_EQ(result.error().source(), "module.luau");
    EXPECT_EQ(result.error().line(), 8);
    EXPECT_EQ(result.error().column(), 2);
}

TEST(ResultTests, RejectsAccessToInactiveAlternative) {
    auto success = lunaris::Result<int>::success(7);
    auto failure = lunaris::Result<int>::failure(
        lunaris::Error(lunaris::ErrorType::Load, "invalid bytecode"));

    EXPECT_THROW((void)success.error(), std::logic_error);
    EXPECT_THROW((void)failure.value(), std::logic_error);
}

TEST(ResultTests, SupportsVoidSuccessAndFailure) {
    auto success = lunaris::Result<void>::success();
    auto failure = lunaris::Result<void>::failure(
        lunaris::Error(lunaris::ErrorType::Type, "expected string"));

    ASSERT_TRUE(success);
    EXPECT_NO_THROW(success.value());
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().type(), lunaris::ErrorType::Type);
    EXPECT_THROW(failure.value(), std::logic_error);
    EXPECT_THROW((void)success.error(), std::logic_error);
}
