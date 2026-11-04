#pragma once

#include "lunaris/Error.h"

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace lunaris {

/// Holds either a successful value or a structured error.
template <typename T> class [[nodiscard]] Result {
    static_assert(!std::is_void_v<T>, "Use Result<void> for void results");
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>,
                  "Result value type cannot be Error");

  public:
    /// Constructs a successful result.
    [[nodiscard]] static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    /// Constructs a failed result.
    [[nodiscard]] static Result failure(Error error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    /// Returns whether this result contains a value.
    [[nodiscard]] bool has_value() const noexcept {
        return m_storage.index() == 0;
    }

    /// Returns whether this result contains a value.
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Returns the value.
    /// @throws std::logic_error if this result contains an error.
    [[nodiscard]] T &value() & {
        if (auto *value = std::get_if<0>(&m_storage)) {
            return *value;
        }
        throw std::logic_error("value() called on a failed Result");
    }

    /// Returns the value.
    /// @throws std::logic_error if this result contains an error.
    [[nodiscard]] const T &value() const & {
        if (const auto *value = std::get_if<0>(&m_storage)) {
            return *value;
        }
        throw std::logic_error("value() called on a failed Result");
    }

    /// Returns the value, allowing it to be moved out.
    /// @throws std::logic_error if this result contains an error.
    [[nodiscard]] T &&value() && {
        if (auto *value = std::get_if<0>(&m_storage)) {
            return std::move(*value);
        }
        throw std::logic_error("value() called on a failed Result");
    }

    /// Returns the error.
    /// @throws std::logic_error if this result contains a value.
    [[nodiscard]] Error &error() & {
        if (auto *error = std::get_if<1>(&m_storage)) {
            return *error;
        }
        throw std::logic_error("error() called on a successful Result");
    }

    /// Returns the error.
    /// @throws std::logic_error if this result contains a value.
    [[nodiscard]] const Error &error() const & {
        if (const auto *error = std::get_if<1>(&m_storage)) {
            return *error;
        }
        throw std::logic_error("error() called on a successful Result");
    }

  private:
    template <std::size_t Index, typename U>
    explicit Result(std::in_place_index_t<Index> index, U &&value)
        : m_storage(index, std::forward<U>(value)) {
    }

    std::variant<T, Error> m_storage;
};

/// Holds either successful completion or a structured error.
template <> class [[nodiscard]] Result<void> {
  public:
    /// Constructs a successful result.
    [[nodiscard]] static Result success() {
        return Result();
    }

    /// Constructs a failed result.
    [[nodiscard]] static Result failure(Error error) {
        return Result(std::move(error));
    }

    /// Returns whether the operation succeeded.
    [[nodiscard]] bool has_value() const noexcept {
        return !m_error.has_value();
    }

    /// Returns whether the operation succeeded.
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Checks successful completion.
    /// @throws std::logic_error if this result contains an error.
    void value() const {
        if (m_error) {
            throw std::logic_error("value() called on a failed Result");
        }
    }

    /// Returns the error.
    /// @throws std::logic_error if this result represents success.
    [[nodiscard]] Error &error() & {
        if (m_error) {
            return *m_error;
        }
        throw std::logic_error("error() called on a successful Result");
    }

    /// Returns the error.
    /// @throws std::logic_error if this result represents success.
    [[nodiscard]] const Error &error() const & {
        if (m_error) {
            return *m_error;
        }
        throw std::logic_error("error() called on a successful Result");
    }

  private:
    Result() = default;
    explicit Result(Error error) : m_error(std::move(error)) {
    }

    std::optional<Error> m_error;
};

} // namespace lunaris
