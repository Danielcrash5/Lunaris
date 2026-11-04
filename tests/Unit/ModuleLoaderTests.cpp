#include "lunaris/ModuleLoader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

class TestModuleProvider final : public lunaris::ModuleProvider {
  public:
    struct Entry {
        std::string root;
        lunaris::ModuleSource module;
    };

    lunaris::Result<std::optional<lunaris::ModuleSource>>
    resolve(std::string_view name, std::string_view searchRoot) override {
        requestedNames.emplace_back(name);
        requestedRoots.emplace_back(searchRoot);
        if (failure) {
            return lunaris::Result<
                std::optional<lunaris::ModuleSource>>::failure(*failure);
        }

        const auto iterator = entries.find(std::string(name));
        if (iterator == entries.end()) {
            return lunaris::Result<
                std::optional<lunaris::ModuleSource>>::success(std::nullopt);
        }
        for (const Entry &entry : iterator->second) {
            if (entry.root == searchRoot) {
                return lunaris::Result<std::optional<lunaris::ModuleSource>>::
                    success(entry.module);
            }
        }
        return lunaris::Result<std::optional<lunaris::ModuleSource>>::success(
            std::nullopt);
    }

    std::unordered_map<std::string, std::vector<Entry>> entries;
    std::vector<std::string> requestedNames;
    std::vector<std::string> requestedRoots;
    std::optional<lunaris::Error> failure;
};

lunaris::ModuleSource make_module(std::string canonicalId,
                                  std::string sourceIdentifier,
                                  std::string source) {
    return {std::move(canonicalId), std::move(sourceIdentifier),
            std::move(source)};
}

TEST(ModuleLoaderTests, ResolvesAcrossConfiguredRoots) {
    auto provider = std::make_shared<TestModuleProvider>();
    provider->entries["Player"] = {
        {"game://modules",
         make_module("game:Player", "game://Player.luau", "return 1")}};
    lunaris::ModuleLoaderConfig config;
    config.basePath = "game://modules";
    config.searchPaths = {"engine://modules"};
    lunaris::ModuleLoader loader(provider, config);

    auto result = loader.resolve("Player");

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().canonicalId, "game:Player");
    EXPECT_EQ(provider->requestedRoots.size(), 2U);
    EXPECT_NE(std::find(provider->requestedRoots.begin(),
                        provider->requestedRoots.end(), "game://modules"),
              provider->requestedRoots.end());
    EXPECT_NE(std::find(provider->requestedRoots.begin(),
                        provider->requestedRoots.end(), "engine://modules"),
              provider->requestedRoots.end());
}

TEST(ModuleLoaderTests, RejectsDistinctMatchesAsAmbiguous) {
    auto provider = std::make_shared<TestModuleProvider>();
    provider->entries["Player"] = {
        {"game://modules",
         make_module("game:Player", "game://Player.luau", "return 1")},
        {"engine://modules",
         make_module("engine:Player", "engine://Player.luau", "return 2")}};
    lunaris::ModuleLoaderConfig config;
    config.basePath = "game://modules";
    config.searchPaths = {"engine://modules"};
    lunaris::ModuleLoader loader(provider, config);

    auto result = loader.resolve("Player");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Module);
    EXPECT_NE(result.error().message().find("ambiguous"),
              std::string_view::npos);
}

TEST(ModuleLoaderTests, DeduplicatesSameCanonicalModuleAcrossRoots) {
    auto provider = std::make_shared<TestModuleProvider>();
    const auto module =
        make_module("shared:Player", "shared://Player.luau", "return 1");
    provider->entries["Player"] = {{"game://modules", module},
                                   {"engine://modules", module}};
    lunaris::ModuleLoaderConfig config;
    config.basePath = "game://modules";
    config.searchPaths = {"engine://modules"};
    lunaris::ModuleLoader loader(provider, config);

    auto result = loader.resolve("Player");

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().canonicalId, "shared:Player");
}

TEST(ModuleLoaderTests, CatalogResolvesWithoutDynamicProviderCalls) {
    auto provider = std::make_shared<TestModuleProvider>();
    lunaris::ModuleLoaderConfig config;
    config.allowDynamicResolution = false;
    config.modules.push_back(
        {"Player", make_module("catalog:Player", "catalog://Player.luau",
                               "return true")});
    lunaris::ModuleLoader loader(provider, config);

    auto result = loader.resolve("Player");

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().canonicalId, "catalog:Player");
    EXPECT_TRUE(provider->requestedNames.empty());
}

TEST(ModuleLoaderTests, RejectsAmbiguousCatalogAndProviderResults) {
    auto provider = std::make_shared<TestModuleProvider>();
    provider->entries["Player"] = {
        {"game://modules",
         make_module("game:Player", "game://Player.luau", "return 1")}};
    lunaris::ModuleLoaderConfig config;
    config.basePath = "game://modules";
    config.modules.push_back(
        {"Player",
         make_module("catalog:Player", "catalog://Player.luau", "return 2")});
    lunaris::ModuleLoader loader(provider, config);

    auto result = loader.resolve("Player");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Module);
    EXPECT_NE(result.error().message().find("ambiguous"),
              std::string_view::npos);
}

TEST(ModuleLoaderTests, ReportsMissingModulesAndProviderErrors) {
    auto provider = std::make_shared<TestModuleProvider>();
    lunaris::ModuleLoader missingLoader(provider);
    auto missing = missingLoader.resolve("Missing");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().type(), lunaris::ErrorType::Module);
    EXPECT_NE(missing.error().message().find("not found"),
              std::string_view::npos);

    provider->failure.emplace(
        lunaris::Error(lunaris::ErrorType::Load, "provider offline"));
    lunaris::ModuleLoader failingLoader(provider);
    auto failed = failingLoader.resolve("Player");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().type(), lunaris::ErrorType::Module);
    EXPECT_NE(failed.error().message().find("provider offline"),
              std::string_view::npos);
}

TEST(ModuleLoaderTests, RejectsEmptyNamesAndCanonicalIds) {
    auto provider = std::make_shared<TestModuleProvider>();
    provider->entries["Player"] = {
        {"", make_module("", "source.luau", "return 1")}};
    lunaris::ModuleLoader loader(provider);

    auto emptyName = loader.resolve("");
    auto emptyCanonicalId = loader.resolve("Player");

    ASSERT_FALSE(emptyName);
    EXPECT_EQ(emptyName.error().type(), lunaris::ErrorType::Module);
    ASSERT_FALSE(emptyCanonicalId);
    EXPECT_EQ(emptyCanonicalId.error().type(), lunaris::ErrorType::Module);
}

} // namespace
