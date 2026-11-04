#include "lunaris/ModuleLoader.h"

#include <algorithm>
#include <string>
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

} // namespace

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

} // namespace lunaris
