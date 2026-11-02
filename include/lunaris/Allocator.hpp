#pragma once

#include <cstddef>

namespace lunaris
{

struct Allocator
{
    using AllocateFn = void* (*)(
        void* userData,
        void* pointer,
        std::size_t oldSize,
        std::size_t newSize
    );

    AllocateFn allocate = nullptr;
    void* userData = nullptr;
};

} // namespace lunaris
