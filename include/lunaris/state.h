#pragma once
#include <lua.h>
#include <memory>

namespace Lunnaris {

/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    State();
    ~State();

  private:
    std::unique_ptr<lua_State> m_luaState;
};
} // namespace Lunnaris
