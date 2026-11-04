#pragma once

#include <string>
#include <string_view>

namespace lunaris {

/// Category of a structured Lunaris or Luau operation error.
enum class ErrorType { Compile, Load, Runtime, Type, Memory, Cache, Module };

/// Structured error information returned by fallible Lunaris operations.
class Error {
  public:
    /// Creates an error with optional source location information.
    Error(ErrorType type, std::string message, std::string source = {},
          int line = -1, int column = -1);

    /// Returns the error category.
    [[nodiscard]] ErrorType type() const noexcept;

    /// Returns the diagnostic message.
    [[nodiscard]] std::string_view message() const noexcept;

    /// Returns the source identifier, or an empty view if unavailable.
    [[nodiscard]] std::string_view source() const noexcept;

    /// Returns the one-based source line, or -1 if unavailable.
    [[nodiscard]] int line() const noexcept;

    /// Returns the one-based source column, or -1 if unavailable.
    [[nodiscard]] int column() const noexcept;

  private:
    ErrorType m_type;
    std::string m_message;
    std::string m_source;
    int m_line;
    int m_column;
};

} // namespace lunaris
