#pragma once

#include <lua.h>
#include <memory>

namespace lunaris {
class State;
} // namespace lunaris

namespace lunaris::detail {

/// @brief Owns a Luau state and closes it when destroyed. Not part of the
/// public API.
///
/// Shared between a State and the objects that outlive it, so that they can
/// detect that the State is gone.
class StateContext {
  public:
    /// @brief Creates a new Luau state.
    /// @throws std::bad_alloc if the state could not be created.
    StateContext();

    /// @brief Destroys the Luau state if it is still open.
    ~StateContext();

    /// @brief Deleted copy constructor. StateContext cannot be copied.
    StateContext(const StateContext &other) = delete;

    /// @brief Deleted copy assignment operator. StateContext cannot be copied.
    StateContext &operator=(const StateContext &other) = delete;

    /// @brief Deleted move constructor. StateContext cannot be moved.
    StateContext(StateContext &&other) = delete;

    /// @brief Deleted move assignment operator. StateContext cannot be moved.
    StateContext &operator=(StateContext &&other) = delete;

    /// @brief Gets a pointer to the underlying lua_State.
    ///
    /// The pointer is only borrowed. Never close the state manually, the
    /// StateContext closes it.
    /// @return A pointer to the lua_State, or nullptr if the state is closed.
    [[nodiscard]] lua_State *luaState() const noexcept;

    /// @brief Checks if the Luau state is open.
    /// @return True if the state is open, false otherwise.
    [[nodiscard]] bool isOpen() const noexcept;

  private:
    friend class ::lunaris::State;

    /// @brief Closes the Luau state. After calling this method, the state is
    /// closed and luaState() returns nullptr. Calling it again has no effect.
    void close() noexcept;

    /// @brief A deleter for lua_State that calls lua_close when the unique_ptr
    /// is destroyed.
    struct LuaDeleter {
        /// @brief Calls lua_close on the given lua_State pointer.
        /// @param state The lua_State pointer to close.
        void operator()(lua_State *state) const noexcept {
            lua_close(state);
        }
    };

    /// @brief A unique_ptr that owns the lua_State and uses LuaDeleter to close
    /// it when destroyed.
    std::unique_ptr<lua_State, LuaDeleter> m_luaState;
};

} // namespace lunaris::detail
