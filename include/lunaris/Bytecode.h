#pragma once

#include "lunaris/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lunaris {

/// Owned, version-checked Luau bytecode.
class Bytecode {
  public:
    /// Compiles Luau source using Luau's default compiler options.
    /// @return Compiled bytecode, or a Compile or Memory error.
    [[nodiscard]] static Result<Bytecode>
    compile(std::string_view source, std::string_view sourceName = {});

    /// Copies bytecode bytes after validating the Luau bytecode version header.
    /// Full bytecode validation occurs when it is loaded into a State.
    /// @return Owned bytecode, or a Load error for an invalid/unsupported
    /// header.
    [[nodiscard]] static Result<Bytecode> from_bytes(std::string_view bytes);

    /// Returns an immutable view of the owned bytecode bytes.
    [[nodiscard]] std::string_view bytes() const noexcept;

    /// Returns the Luau bytecode version stored in the header.
    [[nodiscard]] std::uint8_t version() const noexcept;

    /// Returns the Luau type information version, or zero when absent.
    [[nodiscard]] std::uint8_t type_version() const noexcept;

  private:
    Bytecode(std::string bytes, std::uint8_t version, std::uint8_t typeVersion);

    std::string m_bytes;
    std::uint8_t m_version;
    std::uint8_t m_typeVersion;
};

} // namespace lunaris
