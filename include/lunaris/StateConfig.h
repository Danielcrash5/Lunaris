#pragma once

#include "lunaris/Allocator.h"

#include <cstddef>
#include <optional>

namespace lunaris {

/// Configuration used when constructing a State.
struct StateConfig {
    /// Optional custom allocator. Its user data must outlive the State.
    Allocator allocator{};

    /// Optional hard limit in bytes for allocations made by the Luau state.
    /// Unset means unlimited. Host and native-module allocations that bypass
    /// the Luau allocator are not included.
    std::optional<std::size_t> memoryLimit;
};

} // namespace lunaris
