#pragma once

#include "lunaris/Bytecode.h"
#include "lunaris/Future.h"
#include "lunaris/Result.h"
#include "lunaris/Value.h"

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace lunaris {
class ModuleLoader;
class BytecodeCacheStorage;
namespace detail {
struct SharedTableRegistryData;
}
} // namespace lunaris

namespace lunaris::detail {

struct PendingResume {
    std::shared_ptr<std::atomic_bool> ready;
    std::function<Result<void>(lua_State *)> resume;
};

struct StateLifetime {
    struct ModuleCacheEntry {
        int reference = -1;
        bool loading = false;
        std::unordered_set<std::string> dependencies;
    };

    lua_State *state = nullptr;
    std::shared_ptr<ModuleLoader> moduleLoader;
    std::shared_ptr<BytecodeCacheStorage> bytecodeCacheStorage;
    std::shared_ptr<SharedTableRegistryData> sharedTableRegistry;
    CompilerOptions compilerOptions;
    std::unordered_map<std::string, ModuleCacheEntry> moduleCache;
    std::unordered_map<std::string, int> nativeLibraryModules;
    std::vector<std::string> loadingModules;
    std::vector<std::shared_ptr<void>> bindingRegistrations;
    std::unordered_map<std::string, void *> bindingTypes;
    std::mutex pendingResumesMutex;
    std::list<std::shared_ptr<PendingResume>> pendingResumes;
};

void install_module_loader(lua_State *state,
                           const std::shared_ptr<StateLifetime> &lifetime,
                           std::shared_ptr<ModuleLoader> moduleLoader);
void push_future_userdata(lua_State *state,
                          const std::weak_ptr<StateLifetime> &lifetime,
                          Future<Value> future);
void register_library_module(lua_State *state,
                             const std::shared_ptr<StateLifetime> &lifetime,
                             std::string_view moduleName, int libraryReference,
                             std::string_view globalName, bool moduleOnly);
[[nodiscard]] Result<void> invalidate_module(StateLifetime &lifetime,
                                             std::string_view name);
[[nodiscard]] Result<void> reload_module(StateLifetime &lifetime,
                                         std::string_view name);

} // namespace lunaris::detail
