#pragma once
#include <lua.h>
#include <memory>

namespace lunaris {
namespace detail {
struct LuaDeleter {
    void operator()(lua_State *state) const noexcept {
        lua_close(state);
    }
};
} // namespace detail

/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    State();
    ~State();

    State(State &&other) noexcept;
    State &operator=(State &&other) noexcept;

    State(const State &) = delete;
    State &operator=(const State &) = delete;

    /// @brief returns the underlying lua_State pointer. The pointer is valid as
    /// long as the State object is alive.
    /// @return the underlying lua_State pointer. Is nullptr if the State object
    /// is in a moved-from state.
    lua_State *luaState() const noexcept;

  private:
    std::unique_ptr<lua_State, detail::LuaDeleter> m_luaState;
};
} // namespace lunaris
