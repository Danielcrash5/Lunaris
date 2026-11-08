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
    /// @brief Creates a new Luau state. Throws std::bad_alloc if the state
    /// could not be created.
    State();
    /// @brief Destroys the Luau state.
    ~State() = default;

    /// @brief Move constructor. Transfers ownership of the underlying lua_State
    /// pointer from another State object to this one. The other State object
    /// will be left in a moved-from state, and its luaState() method will
    /// return nullptr.
    /// @param other The State object to move from.
    State(State &&other) noexcept = default;

    /// @brief Move assignment operator. Transfers ownership of the underlying
    /// lua_State pointer from another State object to this one. The other State
    /// object will be left in a moved-from state, and its luaState() method
    /// will return nullptr.
    /// @param other The State object to move from.
    State &operator=(State &&other) noexcept;

    /// @brief Copy constructor: deleted to prevent copying of the State object,
    /// as it owns a unique resource (the lua_State pointer).
    State(const State &) = delete;

    /// @brief Copy assignment operator: deleted to prevent copying of the State
    /// object, as it owns a unique resource (the lua_State pointer).
    State &operator=(const State &) = delete;

    /// @brief returns the underlying lua_State pointer. The pointer is valid as
    /// long as the State object is alive.
    /// @return the underlying lua_State pointer. Is nullptr if the State object
    /// is in a moved-from state.
    [[nodiscard]] lua_State *luaState() const noexcept;

  private:
    /// @brief A unique_ptr that owns the lua_State pointer and automatically
    /// closes it when the State object is destroyed.
    std::unique_ptr<lua_State, detail::LuaDeleter> m_luaState;
};
} // namespace lunaris
