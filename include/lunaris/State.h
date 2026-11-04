#pragma once

#include "lunaris/StateConfig.h"

struct lua_State;

namespace lunaris {

/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    /// Creates a Luau state with the supplied configuration.
    /// @throws std::bad_alloc if the Luau state cannot be created.
    explicit State(const StateConfig &config = {});

    /// Closes the owned Luau state, if any.
    ~State();

    State(const State &) = delete;
    State &operator=(const State &) = delete;

    /// Transfers state ownership from \p other.
    State(State &&other) noexcept;

    /// Closes the current state and transfers ownership from \p other.
    State &operator=(State &&other) noexcept;

    /// Returns the underlying Luau state without transferring ownership.
    /// Returns nullptr after the State has been moved from.
    [[nodiscard]] lua_State *native_handle() noexcept;

  private:
    lua_State* m_state = nullptr;
};

} // namespace lunaris
