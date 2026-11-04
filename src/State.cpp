#include "lunaris/State.h"

#include <lua.h>

#include <cstdlib>
#include <new>
#include <utility>

namespace lunaris {
namespace {

void *default_allocate(void *, void *pointer, std::size_t,
                       std::size_t newSize) {
    if (newSize == 0) {
        std::free(pointer);
        return nullptr;
    }

    return std::realloc(pointer, newSize);
}

} // namespace

State::State(const StateConfig &config) {
    const auto allocate = config.allocator.allocate != nullptr
                              ? config.allocator.allocate
                              : &default_allocate;

    m_state = lua_newstate(allocate, config.allocator.userData);
    if (m_state == nullptr) {
        throw std::bad_alloc{};
    }
}

State::~State() {
    if (m_state != nullptr) {
        lua_close(m_state);
    }
}

State::State(State &&other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)) {
}

State &State::operator=(State &&other) noexcept {
    if (this != &other) {
        if (m_state != nullptr) {
            lua_close(m_state);
        }

        m_state = std::exchange(other.m_state, nullptr);
    }

    return *this;
}

lua_State *State::native_handle() noexcept {
    return m_state;
}

} // namespace lunaris
