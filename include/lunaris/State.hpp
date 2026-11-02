#pragma once

#include "lunaris/StateConfig.hpp"

struct lua_State;

namespace lunaris
{

class State
{
public:
    explicit State(const StateConfig& config = {});
    ~State();

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    State(State&& other) noexcept;
    State& operator=(State&& other) noexcept;

    [[nodiscard]] lua_State* native_handle() noexcept;

private:
    lua_State* state_ = nullptr;
};

} // namespace lunaris
