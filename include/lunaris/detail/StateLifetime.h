#pragma once

struct lua_State;

namespace lunaris::detail {

struct StateLifetime {
    lua_State *state = nullptr;
};

} // namespace lunaris::detail
