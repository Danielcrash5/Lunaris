#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace lunaris {

/// Owned scalar value that can be transferred to or read from a Luau stack.
/// Tables, functions, userdata, and other reference values are represented by
/// dedicated wrapper types rather than this variant.
using Value =
    std::variant<std::monostate, bool, std::int64_t, double, std::string>;

} // namespace lunaris
