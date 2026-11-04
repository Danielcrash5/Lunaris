#pragma once

#include "lunaris/Result.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lunaris {

/// Source and stable identity returned by a module source provider.
struct ModuleSource {
    std::string canonicalId;
    std::string sourceIdentifier;
    std::string source;
};

/// Explicitly registered module source.
struct ModuleDescriptor {
    std::string name;
    ModuleSource module;
};

/// Resolves a requested name under one provider-defined search root.
class ModuleProvider {
  public:
    virtual ~ModuleProvider() = default;

    /// Returns an empty optional when the provider has no matching module.
    [[nodiscard]] virtual Result<std::optional<ModuleSource>>
    resolve(std::string_view name, std::string_view searchRoot) = 0;
};

/// Configuration for provider and catalog resolution.
struct ModuleLoaderConfig {
    /// Optional first search root.
    std::string basePath;

    /// Additional roots checked alongside the base path.
    std::vector<std::string> searchPaths;

    /// Explicit entries are always considered during resolution.
    std::vector<ModuleDescriptor> modules;

    /// If false, only explicit catalog entries are considered.
    bool allowDynamicResolution = true;
};

/// Resolves module names to one canonical source without filesystem coupling.
class ModuleLoader {
  public:
    /// Creates a loader using the supplied provider and resolution policy.
    explicit ModuleLoader(std::shared_ptr<ModuleProvider> provider,
                          ModuleLoaderConfig config = {});

    /// Resolves a name from the catalog and configured provider roots.
    /// Duplicate canonical identities are coalesced; distinct matches are an
    /// ambiguity error.
    [[nodiscard]] Result<ModuleSource> resolve(std::string_view name) const;

  private:
    std::shared_ptr<ModuleProvider> m_provider;
    ModuleLoaderConfig m_config;
};

} // namespace lunaris
