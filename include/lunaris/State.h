#pragma once

#include "lunaris/StateConfig.h"

struct lua_State;

namespace lunaris {

class State {
  public:
    explicit State(const StateConfig &config = {});
    ~State();

    State(const State &) = delete;
    State &operator=(const State &) = delete;

    State(State &&other) noexcept;
    State &operator=(State &&other) noexcept;

    [[nodiscard]] lua_State *native_handle() noexcept;

  private:
    lua_State* m_state = nullptr;
};

} // namespace lunaris
