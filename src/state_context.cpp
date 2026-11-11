#include "lunaris/detail/state_context.h"

#include <lua.h>
#include <lualib.h>
#include <memory>
#include <stdexcept>

namespace lunaris::detail {

StateContext::StateContext() : m_luaState(luaL_newstate()) {
    if (!m_luaState) {
        throw std::bad_alloc();
    }
}

StateContext::~StateContext() {
    close();
}

lua_State *StateContext::luaState() const noexcept {
    return m_luaState.get();
}

bool StateContext::isOpen() const noexcept {
    return m_luaState != nullptr;
}

void StateContext::close() noexcept {
    m_luaState.reset();
}

} // namespace lunaris::detail
