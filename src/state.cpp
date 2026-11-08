#include "lunaris/state.h"

#include <new>
#include <utility>

#include <lua.h>
#include <lualib.h>

namespace lunaris {
State::State() : m_luaState(luaL_newstate()) {
    if (!m_luaState) {
        throw std::bad_alloc();
    }
}

State &State::operator=(State &&other) noexcept {
    m_luaState = std::move(other.m_luaState);
    return *this;
}

lua_State *State::luaState() const noexcept {
    return m_luaState.get();
}
} // namespace lunaris
