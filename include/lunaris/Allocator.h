#pragma once

#include <cstddef>

namespace lunaris {

/// Custom memory allocator used to create and manage a Luau state.
struct Allocator {
    /// Callback matching Luau's allocator contract.
    ///
    /// A zero \p newSize requests deallocation. A nonzero \p newSize requests
    /// allocation or reallocation; return nullptr if the request cannot be
    /// satisfied.
    /// @param userData Opaque context passed unchanged to the callback.
    /// @param pointer Existing allocation, or nullptr for a new allocation.
    /// @param oldSize Previous allocation size as reported by Luau.
    /// @param newSize Requested allocation size; zero means free.
    /// @return Pointer to the resulting allocation, or nullptr on failure.
    using AllocateFn = void *(*)(void *userData, void *pointer,
                                 std::size_t oldSize, std::size_t newSize);

    /// Allocator callback; nullptr selects Lunaris' default allocator.
    AllocateFn allocate = nullptr;

    /// Opaque value passed to \ref allocate.
    void *userData = nullptr;
};

} // namespace lunaris
