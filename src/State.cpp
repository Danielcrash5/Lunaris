#include "lunaris/State.hpp"

#include <lua.h>

#include <cstdlib>
#include <new>
#include <utility>

namespace lunaris
{
namespace
{

void* default_allocate(
    void*,
    void* pointer,
    std::size_t,
    std::size_t newSize
)
{
    if (newSize == 0)
    {
        std::free(pointer);
        return nullptr;
    }

    return std::realloc(pointer, newSize);
}

} // namespace

State::State(const StateConfig& config)
{
    const auto allocate = config.allocator.allocate != nullptr
        ? config.allocator.allocate
        : &default_allocate;

    state_ = lua_newstate(allocate, config.allocator.userData);
    if (state_ == nullptr)
    {
        throw std::bad_alloc{};
    }
}

State::~State()
{
    if (state_ != nullptr)
    {
        lua_close(state_);
    }
}

State::State(State&& other) noexcept
    : state_(std::exchange(other.state_, nullptr))
{
}

State& State::operator=(State&& other) noexcept
{
    if (this != &other)
    {
        if (state_ != nullptr)
        {
            lua_close(state_);
        }

        state_ = std::exchange(other.state_, nullptr);
    }

    return *this;
}

lua_State* State::native_handle() noexcept
{
    return state_;
}

} // namespace lunaris
