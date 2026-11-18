#pragma once

#include <lua.h>

namespace lunaris::detail {

/// Restores a Luau stack to its size at construction.
class StackGuard {
  public:
    explicit StackGuard(lua_State *state) noexcept
        : m_state(state), m_top(lua_gettop(state)) {}

    StackGuard(const StackGuard &) = delete;
    StackGuard &operator=(const StackGuard &) = delete;
    StackGuard(StackGuard &&) = delete;
    StackGuard &operator=(StackGuard &&) = delete;

    ~StackGuard() noexcept {
        lua_settop(m_state, m_top);
    }

    /// Returns the stack size captured when this guard was constructed.
    [[nodiscard]] int getTop() const noexcept {
        return m_top;
    }

  private:
    lua_State *m_state;
    int m_top;
};

} // namespace lunaris::detail
