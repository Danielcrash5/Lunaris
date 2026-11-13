#include <gtest/gtest.h>
#include <lunaris/error.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
using lunaris::Error;
using lunaris::ErrorDetails;
using lunaris::ErrorKind;

// Details with every field set, so that a test can see each one arrive.
ErrorDetails fullDetails() {
    return ErrorDetails{
        .kind = ErrorKind::runtime,
        .message = "chunk:3: boom",
        .chunkName = "chunk",
        .lineNumber = 3U,
        .traceback = "chunk:3 function f\nchunk:5",
    };
}
} // namespace

// --- Contract checked at compile time
// ------------------------------------------

static_assert(std::is_base_of_v<std::runtime_error, Error>);
static_assert(std::is_base_of_v<std::exception, Error>);
static_assert(std::is_same_v<std::underlying_type_t<ErrorKind>, std::uint8_t>);

// Throwing an Error must not be able to terminate the program, and an Error is
// copied when it is thrown.
static_assert(std::is_nothrow_move_constructible_v<Error>);
static_assert(std::is_nothrow_move_assignable_v<Error>);
static_assert(std::is_copy_constructible_v<Error>);

// The accessors only hand out what the Error already holds.
static_assert(noexcept(std::declval<const Error &>().details()));
static_assert(noexcept(std::declval<const Error &>().kind()));
static_assert(noexcept(std::declval<const Error &>().chunkName()));
static_assert(noexcept(std::declval<const Error &>().lineNumber()));
static_assert(noexcept(std::declval<const Error &>().traceback()));
static_assert(
    std::is_same_v<decltype(std::declval<const Error &>().chunkName()),
                   const std::string &>);
static_assert(
    std::is_same_v<decltype(std::declval<const Error &>().traceback()),
                   const std::string &>);
static_assert(
    std::is_same_v<decltype(std::declval<const Error &>().lineNumber()),
                   std::optional<std::uint32_t>>);

// --- Fields
// ------------------------------------------------------------------------

TEST(ErrorTest, ProvidesAllFields) {
    const Error error(fullDetails());

    EXPECT_EQ(error.kind(), ErrorKind::runtime);
    EXPECT_EQ(error.chunkName(), "chunk");
    ASSERT_TRUE(error.lineNumber().has_value());
    EXPECT_EQ(*error.lineNumber(), 3U);
    EXPECT_EQ(error.traceback(), "chunk:3 function f\nchunk:5");
}

TEST(ErrorTest, WhatReturnsTheMessageUnchanged) {
    const Error error(fullDetails());

    EXPECT_STREQ(error.what(), "chunk:3: boom");
}

TEST(ErrorTest, DetailsHoldTheSameValuesAsTheAccessors) {
    const Error error(fullDetails());
    const ErrorDetails &details = error.details();

    EXPECT_EQ(details.kind, error.kind());
    EXPECT_EQ(details.message, error.what());
    EXPECT_EQ(details.chunkName, error.chunkName());
    EXPECT_EQ(details.lineNumber, error.lineNumber());
    EXPECT_EQ(details.traceback, error.traceback());
}

// The accessors return references to the stored values, they do not copy.
TEST(ErrorTest, AccessorsReturnReferencesIntoTheError) {
    const Error error(fullDetails());

    EXPECT_EQ(&error.chunkName(), &error.details().chunkName);
    EXPECT_EQ(&error.traceback(), &error.details().traceback);
}

// --- Defaults and optional values
// ---------------------------------------------

TEST(ErrorTest, FieldsThatWereNotSetAreEmpty) {
    const Error error(ErrorDetails{.kind = ErrorKind::compile});

    EXPECT_STREQ(error.what(), "");
    EXPECT_TRUE(error.chunkName().empty());
    EXPECT_FALSE(error.lineNumber().has_value());
    EXPECT_TRUE(error.traceback().empty());
}

TEST(ErrorTest, LineNumberZeroIsDifferentFromNoLineNumber) {
    const Error withZero(ErrorDetails{.lineNumber = 0U});
    const Error without(ErrorDetails{});

    ASSERT_TRUE(withZero.lineNumber().has_value());
    EXPECT_EQ(*withZero.lineNumber(), 0U);
    EXPECT_FALSE(without.lineNumber().has_value());
}

TEST(ErrorTest, LineNumberKeepsTheLargestValue) {
    constexpr std::uint32_t largest = std::numeric_limits<std::uint32_t>::max();
    const Error error(ErrorDetails{.lineNumber = largest});

    ASSERT_TRUE(error.lineNumber().has_value());
    EXPECT_EQ(*error.lineNumber(), largest);
}

// --- Kinds
// -------------------------------------------------------------------------

TEST(ErrorTest, EveryKindCanBeStoredAndRead) {
    for (const ErrorKind kind :
         {ErrorKind::compile, ErrorKind::runtime, ErrorKind::memory}) {
        const Error error(ErrorDetails{.kind = kind});

        EXPECT_EQ(error.kind(), kind);
        EXPECT_EQ(error.details().kind, kind);
    }
}

// --- The Error owns its values
// -----------------------------------------------------

TEST(ErrorTest, ChangingTheDetailsAfterwardsDoesNotChangeTheError) {
    ErrorDetails details = fullDetails();
    const Error error(details);

    details.message = "changed";
    details.chunkName = "changed";
    details.lineNumber = 99U;
    details.traceback = "changed";

    EXPECT_STREQ(error.what(), "chunk:3: boom");
    EXPECT_EQ(error.chunkName(), "chunk");
    EXPECT_EQ(error.lineNumber(), 3U);
    EXPECT_EQ(error.traceback(), "chunk:3 function f\nchunk:5");
}

// --- Copy and move
// -----------------------------------------------------------------

TEST(ErrorTest, CopyKeepsAllFieldsAndLeavesTheOriginalIntact) {
    const Error original(fullDetails());

    // The copy is the point of this test.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Error copy(original);

    EXPECT_STREQ(copy.what(), "chunk:3: boom");
    EXPECT_EQ(copy.kind(), ErrorKind::runtime);
    EXPECT_EQ(copy.chunkName(), "chunk");
    EXPECT_EQ(copy.lineNumber(), 3U);
    EXPECT_EQ(copy.traceback(), "chunk:3 function f\nchunk:5");
    EXPECT_STREQ(original.what(), "chunk:3: boom");
    EXPECT_EQ(original.chunkName(), "chunk");
}

TEST(ErrorTest, MoveKeepsAllFields) {
    Error original(fullDetails());

    const Error moved(std::move(original));

    EXPECT_STREQ(moved.what(), "chunk:3: boom");
    EXPECT_EQ(moved.kind(), ErrorKind::runtime);
    EXPECT_EQ(moved.chunkName(), "chunk");
    EXPECT_EQ(moved.lineNumber(), 3U);
    EXPECT_EQ(moved.traceback(), "chunk:3 function f\nchunk:5");
}

TEST(ErrorTest, AssignmentReplacesAllFields) {
    Error target(ErrorDetails{.kind = ErrorKind::memory, .message = "old"});

    target = Error(fullDetails());

    EXPECT_STREQ(target.what(), "chunk:3: boom");
    EXPECT_EQ(target.kind(), ErrorKind::runtime);
    EXPECT_EQ(target.chunkName(), "chunk");
}

// --- Throwing and catching
// -----------------------------------------------------------

TEST(ErrorTest, CanBeCaughtAsError) {
    try {
        throw Error(fullDetails());
    } catch (const Error &error) {
        EXPECT_EQ(error.kind(), ErrorKind::runtime);
        EXPECT_EQ(error.chunkName(), "chunk");
        EXPECT_EQ(error.lineNumber(), 3U);
        EXPECT_EQ(error.traceback(), "chunk:3 function f\nchunk:5");
        return;
    }
    FAIL() << "the Error was not caught";
}

TEST(ErrorTest, CanBeCaughtAsRuntimeError) {
    try {
        throw Error(fullDetails());
    } catch (const std::runtime_error &error) {
        EXPECT_STREQ(error.what(), "chunk:3: boom");
        return;
    }
    FAIL() << "the Error was not caught";
}

TEST(ErrorTest, CanBeCaughtAsException) {
    try {
        throw Error(fullDetails());
    } catch (const std::exception &error) {
        EXPECT_STREQ(error.what(), "chunk:3: boom");
        return;
    }
    FAIL() << "the Error was not caught";
}

// --- Messages
// ------------------------------------------------------------------------

TEST(ErrorTest, EmptyMessageIsAllowed) {
    const Error error(ErrorDetails{.kind = ErrorKind::runtime});

    EXPECT_STREQ(error.what(), "");
    EXPECT_TRUE(error.details().message.empty());
}

// what() is a C string and ends at the first '\0'. The details keep the whole
// message. This documents the limit instead of leaving it a surprise.
TEST(ErrorTest, MessageWithEmbeddedNulIsCutInWhatButKeptInDetails) {
    const std::string message("a\0b", 3);
    const Error error(ErrorDetails{.message = message});

    EXPECT_EQ(std::strlen(error.what()), 1U);
    EXPECT_STREQ(error.what(), "a");
    EXPECT_EQ(error.details().message, message);
    EXPECT_EQ(error.details().message.size(), 3U);
}

TEST(ErrorTest, LongTracebackIsKept) {
    std::string traceback;
    for (int i = 0; i < 1000; ++i) {
        traceback += "chunk:" + std::to_string(i) + " function f\n";
    }

    const Error error(ErrorDetails{.traceback = traceback});

    EXPECT_EQ(error.traceback(), traceback);
}

// --- Use in std::expected
// ----------------------------------------------------------

TEST(ErrorTest, WorksAsTheErrorOfAnExpected) {
    const std::expected<int, Error> value = 42;
    const std::expected<int, Error> failure =
        std::unexpected(Error(fullDetails()));

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 42);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().kind(), ErrorKind::runtime);
    EXPECT_STREQ(failure.error().what(), "chunk:3: boom");
}
