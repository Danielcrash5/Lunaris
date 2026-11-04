#include "lunaris/LuaRocksProvider.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace lunaris {
namespace {

Error provider_error(std::string message, std::string source = {}) {
    return Error(ErrorType::Module, std::move(message), std::move(source));
}

bool valid_module_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    std::size_t componentStart = 0;
    for (std::size_t index = 0; index <= name.size(); ++index) {
        if (index != name.size() && name[index] != '.') {
            const unsigned char character =
                static_cast<unsigned char>(name[index]);
            if (!std::isalnum(character) && character != '_' &&
                character != '-') {
                return false;
            }
            continue;
        }

        if (index == componentStart) {
            return false;
        }
        componentStart = index + 1;
    }
    return true;
}

bool is_within_root(const std::filesystem::path &root,
                    const std::filesystem::path &candidate) {
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

} // namespace

Result<std::optional<ModuleSource>>
LuaRocksModuleProvider::resolve(std::string_view name,
                                std::string_view searchRoot) {
    if (!valid_module_name(name)) {
        return Result<std::optional<ModuleSource>>::failure(provider_error(
            "invalid LuaRocks module name '" + std::string(name) + "'"));
    }
    if (searchRoot.empty() || searchRoot.find('\0') != std::string_view::npos) {
        return Result<std::optional<ModuleSource>>::failure(provider_error(
            "LuaRocks module search root must be a non-empty path"));
    }

    const std::filesystem::path rootPath(searchRoot);
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(rootPath, error);
    if (error) {
        return Result<std::optional<ModuleSource>>::failure(provider_error(
            "cannot resolve LuaRocks module search root: " + error.message(),
            std::string(searchRoot)));
    }

    std::filesystem::path modulePath = root;
    std::size_t componentStart = 0;
    for (std::size_t index = 0; index <= name.size(); ++index) {
        if (index == name.size() || name[index] == '.') {
            modulePath /= std::string(
                name.substr(componentStart, index - componentStart));
            componentStart = index + 1;
        }
    }
    std::filesystem::path luauModule = modulePath;
    luauModule.replace_extension(".luau");
    std::filesystem::path luaModule = modulePath;
    luaModule.replace_extension(".lua");
    const std::vector<std::filesystem::path> candidates{
        luauModule, luaModule, modulePath / "init.luau",
        modulePath / "init.lua"};

    for (const auto &candidate : candidates) {
        error.clear();
        if (!std::filesystem::is_regular_file(candidate, error)) {
            if (error == std::errc::no_such_file_or_directory) {
                error.clear();
                continue;
            }
            if (error) {
                return Result<std::optional<ModuleSource>>::failure(
                    provider_error("cannot inspect LuaRocks module: " +
                                       error.message(),
                                   candidate.string()));
            }
            continue;
        }

        error.clear();
        const auto canonical = std::filesystem::canonical(candidate, error);
        if (error) {
            return Result<std::optional<ModuleSource>>::failure(provider_error(
                "cannot resolve LuaRocks module: " + error.message(),
                candidate.string()));
        }
        if (!is_within_root(root, canonical)) {
            return Result<std::optional<ModuleSource>>::failure(
                provider_error("LuaRocks module resolves outside its search "
                               "root",
                               canonical.string()));
        }

        std::ifstream input(canonical, std::ios::binary);
        if (!input) {
            return Result<std::optional<ModuleSource>>::failure(provider_error(
                "cannot open LuaRocks module source", canonical.string()));
        }
        std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        if (input.bad()) {
            return Result<std::optional<ModuleSource>>::failure(provider_error(
                "cannot read LuaRocks module source", canonical.string()));
        }

        const std::string identifier = canonical.string();
        return Result<std::optional<ModuleSource>>::success(ModuleSource{
            "luarocks:" + identifier, identifier, std::move(source)});
    }

    for (const std::string_view extension : {".dll", ".so", ".dylib"}) {
        std::filesystem::path nativeModule = modulePath;
        nativeModule.replace_extension(extension);
        error.clear();
        if (std::filesystem::is_regular_file(nativeModule, error)) {
            return Result<std::optional<ModuleSource>>::failure(provider_error(
                "native Lua modules are not supported by the LuaRocks source "
                "provider",
                nativeModule.string()));
        }
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            continue;
        }
        if (error) {
            return Result<std::optional<ModuleSource>>::failure(provider_error(
                "cannot inspect native LuaRocks module: " + error.message(),
                nativeModule.string()));
        }
    }

    return Result<std::optional<ModuleSource>>::success(std::nullopt);
}

} // namespace lunaris
