#pragma once
#include <lua.h>
#include <memory>

namespace Lunnaris {

namespace {
struct LuaDeleter {
    void operator()(lua_State *state) const noexcept {
        lua_close(state);
    }
};
} // namespace

/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    State();
    ~State();
    State(State &&other) noexcept;
    State &operator=(State &&other) noexcept;
    State(const State &) = delete;
    State &operator=(const State &) = delete;

  private:
    std::unique_ptr<lua_State, LuaDeleter> m_luaState;
};
} // namespace Lunnaris
