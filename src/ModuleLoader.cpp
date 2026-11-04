#include "lunaris/ModuleLoader.h"

#include "lunaris/Bytecode.h"
#include "lunaris/detail/StateLifetime.h"

#include <lua.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lunaris {
namespace {

Error module_error(std::string message) {
    return Error(ErrorType::Module, std::move(message));
}

bool same_source(const ModuleSource &left, const ModuleSource &right) {
    return left.canonicalId == right.canonicalId &&
           left.sourceIdentifier == right.sourceIdentifier &&
           left.source == right.source;
}

void clear_module_cache(lua_State *state, detail::StateLifetime &lifetime) {
    for (const auto &[canonicalId, entry] : lifetime.moduleCache) {
        (void)canonicalId;
        if (!entry.loading && entry.reference != LUA_NOREF &&
            entry.reference != LUA_REFNIL) {
            lua_unref(state, entry.reference);
        }
    }
    lifetime.moduleCache.clear();
}

class LoadingModuleGuard {
  public:
    LoadingModuleGuard(detail::StateLifetime &lifetime,
                       std::string_view canonicalId)
        : m_lifetime(lifetime) {
        m_lifetime.loadingModules.emplace_back(canonicalId);
    }

    ~LoadingModuleGuard() {
        if (!m_lifetime.loadingModules.empty()) {
            m_lifetime.loadingModules.pop_back();
        }
    }

    LoadingModuleGuard(const LoadingModuleGuard &) = delete;
    LoadingModuleGuard &operator=(const LoadingModuleGuard &) = delete;

  private:
    detail::StateLifetime &m_lifetime;
};

void record_dependency(detail::StateLifetime &lifetime,
                       std::string_view parentId,
                       const std::string &dependencyId) {
    const auto parent = lifetime.moduleCache.find(std::string(parentId));
    if (parent != lifetime.moduleCache.end() && parent->second.loading) {
        parent->second.dependencies.insert(dependencyId);
    }
}

int require_module_impl(lua_State *state, detail::StateLifetime &lifetime,
                        std::string_view name, std::string &error) {
    const int initialTop = lua_gettop(state);
    auto resolved = lifetime.moduleLoader->resolve(name);
    if (!resolved) {
        error = std::string(resolved.error().message());
        lua_settop(state, initialTop);
        return 0;
    }

    ModuleSource module = std::move(resolved).value();
    const auto cached = lifetime.moduleCache.find(module.canonicalId);
    if (cached != lifetime.moduleCache.end()) {
        if (cached->second.loading) {
            error = "cyclic require detected for module '" +
                    module.canonicalId + "'";
            lua_settop(state, initialTop);
            return 0;
        }
        if (!lifetime.loadingModules.empty()) {
            record_dependency(lifetime, lifetime.loadingModules.back(),
                              module.canonicalId);
        }
        lua_getref(state, cached->second.reference);
        return 1;
    }

    if (module.sourceIdentifier.find('\0') != std::string::npos) {
        error = "module source identifier contains a null byte";
        lua_settop(state, initialTop);
        return 0;
    }

    auto bytecode = Bytecode::compile(module.source, module.sourceIdentifier);
    if (!bytecode) {
        error = std::string(bytecode.error().message());
        lua_settop(state, initialTop);
        return 0;
    }

    lua_newtable(state);
    const int environmentIndex = lua_gettop(state);
    lua_newtable(state);
    lua_pushvalue(state, LUA_GLOBALSINDEX);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, environmentIndex);
    lua_setsafeenv(state, environmentIndex, false);

    const std::string_view bytes = bytecode.value().bytes();
    const int loadStatus =
        luau_load(state, module.sourceIdentifier.c_str(), bytes.data(),
                  bytes.size(), environmentIndex);
    if (loadStatus != LUA_OK) {
        std::size_t length = 0;
        const char *message = lua_tolstring(state, -1, &length);
        error = message != nullptr ? std::string(message, length)
                                   : "failed to load module bytecode";
        lua_settop(state, initialTop);
        return 0;
    }

    LoadingModuleGuard loading(lifetime, module.canonicalId);
    lifetime.moduleCache.emplace(
        module.canonicalId,
        detail::StateLifetime::ModuleCacheEntry{LUA_NOREF, true, {}});
    const int status = lua_pcall(state, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        std::size_t length = 0;
        const char *message = lua_tolstring(state, -1, &length);
        error = message != nullptr ? std::string(message, length)
                                   : "module execution failed";
        lifetime.moduleCache.erase(module.canonicalId);
        lua_settop(state, initialTop);
        return 0;
    }

    const int resultCount = lua_gettop(state) - initialTop - 1;
    if (resultCount == 0) {
        lua_pushboolean(state, true);
    } else {
        lua_pushvalue(state, initialTop + 2);
    }
    const int reference = lua_ref(state, -1);
    const auto cacheEntry = lifetime.moduleCache.find(module.canonicalId);
    if (cacheEntry == lifetime.moduleCache.end()) {
        error = "module loader changed while module '" + module.canonicalId +
                "' was loading";
        lua_unref(state, reference);
        lua_settop(state, initialTop);
        return 0;
    }
    cacheEntry->second.reference = reference;
    cacheEntry->second.loading = false;
    if (lifetime.loadingModules.size() > 1) {
        record_dependency(
            lifetime,
            lifetime.loadingModules[lifetime.loadingModules.size() - 2],
            module.canonicalId);
    }
    lua_settop(state, initialTop);
    lua_getref(state, reference);
    return 1;
}

Result<void> invalidate_module_cache(detail::StateLifetime &lifetime,
                                     std::string_view name) {
    if (!lifetime.moduleLoader) {
        return Result<void>::failure(
            module_error("module loader is not available"));
    }
    if (name.find('\0') != std::string_view::npos) {
        return Result<void>::failure(
            module_error("module name contains a null byte"));
    }

    auto resolved = lifetime.moduleLoader->resolve(name);
    if (!resolved) {
        return Result<void>::failure(resolved.error());
    }

    std::unordered_set<std::string> invalidated;
    invalidated.insert(resolved.value().canonicalId);
    bool foundDependent = true;
    while (foundDependent) {
        foundDependent = false;
        for (const auto &[canonicalId, entry] : lifetime.moduleCache) {
            if (invalidated.contains(canonicalId)) {
                continue;
            }
            if (std::any_of(entry.dependencies.begin(),
                            entry.dependencies.end(),
                            [&invalidated](const std::string &dependency) {
                                return invalidated.contains(dependency);
                            })) {
                invalidated.insert(canonicalId);
                foundDependent = true;
            }
        }
    }

    for (const std::string &canonicalId : invalidated) {
        const auto entry = lifetime.moduleCache.find(canonicalId);
        if (entry != lifetime.moduleCache.end() && entry->second.loading) {
            return Result<void>::failure(
                module_error("cannot invalidate module '" + canonicalId +
                             "' while it is loading"));
        }
    }

    for (const std::string &canonicalId : invalidated) {
        const auto entry = lifetime.moduleCache.find(canonicalId);
        if (entry == lifetime.moduleCache.end()) {
            continue;
        }
        if (entry->second.reference != LUA_NOREF &&
            entry->second.reference != LUA_REFNIL) {
            lua_unref(lifetime.state, entry->second.reference);
        }
        lifetime.moduleCache.erase(entry);
    }
    return Result<void>::success();
}

int require_module(lua_State *state) {
    auto *lifetime = static_cast<detail::StateLifetime *>(
        lua_touserdata(state, lua_upvalueindex(1)));
    if (lifetime == nullptr || lifetime->state != state ||
        !lifetime->moduleLoader) {
        lua_pushstring(state, "Lunaris module loader is not available");
        lua_error(state);
        return 0;
    }

    if (lua_type(state, 1) != LUA_TSTRING) {
        lua_pushstring(state, "require expects a string module name");
        lua_error(state);
        return 0;
    }

    std::size_t length = 0;
    const char *nameData = lua_tolstring(state, 1, &length);
    if (std::string_view(nameData, length).find('\0') !=
        std::string_view::npos) {
        lua_pushstring(state, "module name contains a null byte");
        lua_error(state);
        return 0;
    }

    bool failed = false;
    int resultCount = 0;
    {
        const std::string name(nameData, length);
        std::string error;
        try {
            resultCount = require_module_impl(state, *lifetime, name, error);
        } catch (const std::exception &exception) {
            error = exception.what();
        }
        if (!error.empty()) {
            lua_pushlstring(state, error.data(), error.size());
            failed = true;
        }
    }

    if (failed) {
        lua_error(state);
        return 0;
    }
    return resultCount;
}

} // namespace

Result<void> detail::invalidate_module(StateLifetime &lifetime,
                                       std::string_view name) {
    return invalidate_module_cache(lifetime, name);
}

Result<void> detail::reload_module(StateLifetime &lifetime,
                                   std::string_view name) {
    auto invalidated = invalidate_module_cache(lifetime, name);
    if (!invalidated) {
        return invalidated;
    }

    const int initialTop = lua_gettop(lifetime.state);
    std::string error;
    try {
        (void)require_module_impl(lifetime.state, lifetime, name, error);
    } catch (const std::exception &exception) {
        error = exception.what();
    }
    lua_settop(lifetime.state, initialTop);
    if (!error.empty()) {
        return Result<void>::failure(module_error(std::move(error)));
    }
    return Result<void>::success();
}

ModuleLoader::ModuleLoader(std::shared_ptr<ModuleProvider> provider,
                           ModuleLoaderConfig config)
    : m_provider(std::move(provider)), m_config(std::move(config)) {
}

Result<ModuleSource> ModuleLoader::resolve(std::string_view name) const {
    if (name.empty()) {
        return Result<ModuleSource>::failure(
            module_error("module name cannot be empty"));
    }

    std::vector<ModuleSource> matches;
    for (const ModuleDescriptor &descriptor : m_config.modules) {
        if (descriptor.name == name) {
            matches.push_back(descriptor.module);
        }
    }

    if (m_config.allowDynamicResolution && m_provider) {
        std::vector<std::string> roots;
        if (!m_config.basePath.empty()) {
            roots.push_back(m_config.basePath);
        }
        roots.insert(roots.end(), m_config.searchPaths.begin(),
                     m_config.searchPaths.end());
        if (roots.empty()) {
            roots.emplace_back();
        }
        std::sort(roots.begin(), roots.end());
        roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

        for (const std::string &root : roots) {
            auto result = m_provider->resolve(name, root);
            if (!result) {
                Error error = result.error();
                return Result<ModuleSource>::failure(module_error(
                    "module provider failed for '" + std::string(name) +
                    "': " + std::string(error.message())));
            }
            if (result.value().has_value()) {
                matches.push_back(std::move(*result.value()));
            }
        }
    }

    std::vector<ModuleSource> uniqueMatches;
    for (ModuleSource &candidate : matches) {
        if (candidate.canonicalId.empty()) {
            return Result<ModuleSource>::failure(
                module_error("module provider returned an empty canonical ID"));
        }

        const auto existing =
            std::find_if(uniqueMatches.begin(), uniqueMatches.end(),
                         [&candidate](const ModuleSource &match) {
                             return match.canonicalId == candidate.canonicalId;
                         });
        if (existing == uniqueMatches.end()) {
            uniqueMatches.push_back(std::move(candidate));
        } else if (!same_source(*existing, candidate)) {
            return Result<ModuleSource>::failure(module_error(
                "module providers returned conflicting sources for canonical "
                "ID '" +
                candidate.canonicalId + "'"));
        }
    }

    if (uniqueMatches.empty()) {
        return Result<ModuleSource>::failure(
            module_error("module '" + std::string(name) + "' was not found"));
    }
    if (uniqueMatches.size() > 1) {
        return Result<ModuleSource>::failure(
            module_error("module '" + std::string(name) +
                         "' is ambiguous across configured sources"));
    }

    return Result<ModuleSource>::success(std::move(uniqueMatches.front()));
}

void detail::install_module_loader(
    lua_State *state, const std::shared_ptr<detail::StateLifetime> &lifetime,
    std::shared_ptr<ModuleLoader> moduleLoader) {
    clear_module_cache(state, *lifetime);
    lifetime->moduleLoader = std::move(moduleLoader);
    if (lifetime->moduleLoader) {
        lua_pushlightuserdata(state, lifetime.get());
        lua_pushcclosure(state, require_module, "require", 1);
        lua_setglobal(state, "require");
    } else {
        lua_pushnil(state);
        lua_setglobal(state, "require");
    }
}

} // namespace lunaris
