#pragma once

#include "lunaris/Allocator.h"

namespace lunaris {

/// Configuration used when constructing a State.
struct StateConfig {
    /// Optional custom allocator. Its user data must outlive the State.
    Allocator allocator{};
};

} // namespace lunaris
