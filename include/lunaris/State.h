#pragma once

#include "lunaris/StateConfig.h"

#include <cstddef>
#include <memory>

struct lua_State;

namespace lunaris {

/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    /// Creates a Luau state with the supplied configuration.
    /// @throws std::bad_alloc if the state cannot be created or its initial
    /// allocations exceed the configured memory limit.
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
    struct AllocatorContext;

    static void *tracked_allocate(void *userData, void *pointer,
                                  std::size_t oldSize, std::size_t newSize);

    lua_State* m_state = nullptr;
    std::unique_ptr<AllocatorContext> m_allocatorContext;
};

} // namespace lunaris
