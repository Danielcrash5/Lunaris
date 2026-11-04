#pragma once

#include <memory>
#include <string>
#include <unordered_map>

struct lua_State;

namespace lunaris {
class ModuleLoader;
}

namespace lunaris::detail {

struct StateLifetime {
    struct ModuleCacheEntry {
        int reference = -1;
        bool loading = false;
    };

    lua_State *state = nullptr;
    std::shared_ptr<ModuleLoader> moduleLoader;
    std::unordered_map<std::string, ModuleCacheEntry> moduleCache;
};

void install_module_loader(lua_State *state,
                           const std::shared_ptr<StateLifetime> &lifetime,
                           std::shared_ptr<ModuleLoader> moduleLoader);

} // namespace lunaris::detail
