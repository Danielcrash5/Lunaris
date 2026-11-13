#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace lunaris {
/// @brief The kind of error that occurred in a Luau state.
enum class ErrorKind : std::uint8_t {
    compile, ///< Compilation error.
    runtime, ///< Runtime error.
    memory,  ///< Memory allocation error.
};

// clang-format off
/// @brief Details about an error that occurred in a Luau state.
struct ErrorDetails {
    ErrorKind kind = ErrorKind::compile;        ///< The kind of error.
    std::string message{};                      ///< The error message.
    std::string chunkName{};                    ///< The name of the chunk where the error occurred.
    std::optional<std::uint32_t> lineNumber{};  ///< The line number where the error occurred, if available.
    std::string traceback{};                    ///< The traceback of the error, if available.
};
// clang-format on

/// @brief An exception that is thrown when an error occurs in a Luau state.
class Error : public std::runtime_error {
  public:
    /// @brief Constructs an Error object with the given error details.
    /// @param details The error details.
    explicit Error(const ErrorDetails &details);

    /// @brief Gets the error details.
    /// @return The error details.
    [[nodiscard]] const ErrorDetails &details() const;

    /// @brief  Gets the kind of error that occurred.
    /// @return The kind of error.
    [[nodiscard]] ErrorKind kind() const noexcept;
    /// @brief Gets the name of the chunk where the error occurred.
    /// @return The chunk name.
    [[nodiscard]] const std::string &chunkName() const noexcept;
    /// @brief Gets the line number where the error occurred.
    /// @return The line number, if available.
    [[nodiscard]] std::optional<std::uint32_t> lineNumber() const noexcept;
    /// @brief Gets the traceback of the error.
    /// @return The traceback.
    [[nodiscard]] const std::string &traceback() const noexcept;

  private:
    ErrorDetails m_details; ///< The error details.
};
} // namespace lunaris
