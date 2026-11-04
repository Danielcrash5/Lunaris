#pragma once

#include "lunaris/ModuleLoader.h"

#if defined(LUNARIS_ENABLE_LUAROCKS)

namespace lunaris {

/// Resolves installed LuaRocks source files from a module search root.
/// Only .luau and .lua source files are supported; native Lua modules are not
/// loaded.
class LuaRocksModuleProvider final : public ModuleProvider {
  public:
    /// Resolves dotted names as paths under \p searchRoot, for example
    /// "foo.bar" to foo/bar.lua or foo/bar/init.lua.
    [[nodiscard]] Result<std::optional<ModuleSource>>
    resolve(std::string_view name, std::string_view searchRoot) override;
};

} // namespace lunaris

#endif
