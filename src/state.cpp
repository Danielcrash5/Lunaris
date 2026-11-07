#include "lunaris/state.h"

#include <lua.h>
#include <lualib.h>

namespace lunaris {
State::State() : m_luaState(luaL_newstate()) {
    if (!m_luaState) {
        throw std::runtime_error("Failed to create Lua state");
    }
}

State::~State() {
    // The unique_ptr will automatically call the LuaDeleter to close the state
}

State::State(State &&other) noexcept : m_luaState(std::move(other.m_luaState)) {
    // Move constructor
}

State &State::operator=(State &&other) noexcept {
    if (this != &other) {
        m_luaState = std::move(other.m_luaState);
    }
    return *this;
}

lua_State *State::luaState() const noexcept {
    return m_luaState.get();
}
} // namespace lunaris
