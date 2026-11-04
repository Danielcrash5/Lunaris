#pragma once

#include "lunaris/Result.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace lunaris {
class ModuleLoader;
}

namespace lunaris::detail {

struct StateLifetime {
    struct ModuleCacheEntry {
        int reference = -1;
        bool loading = false;
        std::unordered_set<std::string> dependencies;
    };

    lua_State *state = nullptr;
    std::shared_ptr<ModuleLoader> moduleLoader;
    std::unordered_map<std::string, ModuleCacheEntry> moduleCache;
    std::vector<std::string> loadingModules;
};

void install_module_loader(lua_State *state,
                           const std::shared_ptr<StateLifetime> &lifetime,
                           std::shared_ptr<ModuleLoader> moduleLoader);
[[nodiscard]] Result<void> invalidate_module(StateLifetime &lifetime,
                                             std::string_view name);
[[nodiscard]] Result<void> reload_module(StateLifetime &lifetime,
                                         std::string_view name);

} // namespace lunaris::detail
