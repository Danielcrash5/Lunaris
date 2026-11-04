#include "lunaris/ModuleLoader.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

class SourceProvider final : public lunaris::ModuleProvider {
  public:
    lunaris::Result<std::optional<lunaris::ModuleSource>>
    resolve(std::string_view name, std::string_view) override {
        const auto iterator = sources.find(std::string(name));
        if (iterator == sources.end()) {
            return lunaris::Result<
                std::optional<lunaris::ModuleSource>>::success(std::nullopt);
        }
        return lunaris::Result<std::optional<lunaris::ModuleSource>>::success(
            iterator->second);
    }

    std::unordered_map<std::string, lunaris::ModuleSource> sources;
};

lunaris::Result<std::vector<lunaris::Value>>
execute_require(lunaris::State &state, std::string_view requestedName) {
    const std::string source =
        "return require(\"" + std::string(requestedName) + "\")()";
    auto script = state.load_script(source, "require-test.luau");
    if (!script) {
        return lunaris::Result<std::vector<lunaris::Value>>::failure(
            script.error());
    }
    return script.value().execute();
}

} // namespace

TEST(ModuleRequireTests, ExecutesAndCachesModulesByCanonicalId) {
    auto provider = std::make_shared<SourceProvider>();
    provider->sources.emplace(
        "Counter",
        lunaris::ModuleSource{"game:Counter", "game://modules/Counter.luau",
                              "local count = 0.0; return function() count += "
                              "1; return count end"});
    lunaris::ModuleLoaderConfig config;
    config.basePath = "game://modules";
    auto loader =
        std::make_shared<lunaris::ModuleLoader>(provider, std::move(config));
    lunaris::State state;
    state.set_module_loader(loader);

    auto first = execute_require(state, "Counter");
    auto second = execute_require(state, "Counter");

    ASSERT_TRUE(first) << first.error().message();
    ASSERT_TRUE(second) << second.error().message();
    EXPECT_EQ(std::get<double>(first.value()[0]), 1.0);
    EXPECT_EQ(std::get<double>(second.value()[0]), 2.0);
}

TEST(ModuleRequireTests, ReportsMissingModulesAndInvalidArguments) {
    auto loader = std::make_shared<lunaris::ModuleLoader>(nullptr);
    lunaris::State state;
    state.set_module_loader(loader);

    auto missing =
        state.load_script("return require('missing')", "missing-require.luau");
    ASSERT_TRUE(missing);
    auto missingResult = missing.value().execute();
    ASSERT_FALSE(missingResult);
    EXPECT_EQ(missingResult.error().type(), lunaris::ErrorType::Runtime);
    EXPECT_NE(missingResult.error().message().find("not found"),
              std::string_view::npos);

    auto badArgument =
        state.load_script("return require({})", "bad-require.luau");
    ASSERT_TRUE(badArgument);
    auto badArgumentResult = badArgument.value().execute();
    ASSERT_FALSE(badArgumentResult);
    EXPECT_NE(badArgumentResult.error().message().find("expects a string"),
              std::string_view::npos);
}

TEST(ModuleRequireTests, ModulesWithoutReturnValueCacheTrue) {
    auto provider = std::make_shared<SourceProvider>();
    provider->sources.emplace(
        "SideEffect",
        lunaris::ModuleSource{"game:SideEffect", "game://SideEffect.luau",
                              "local initialized = true"});
    auto loader = std::make_shared<lunaris::ModuleLoader>(provider);
    lunaris::State state;
    state.set_module_loader(loader);
    auto script =
        state.load_script("return require('SideEffect'), require('SideEffect')",
                          "no-return-require.luau");
    ASSERT_TRUE(script);

    auto result = script.value().execute();

    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_TRUE(std::get<bool>(result.value()[0]));
    EXPECT_TRUE(std::get<bool>(result.value()[1]));
}

TEST(ModuleRequireTests, DetectsCyclesAndDoesNotCacheFailures) {
    auto provider = std::make_shared<SourceProvider>();
    provider->sources.emplace("A",
                              lunaris::ModuleSource{"game:A", "game://A.luau",
                                                    "require('B'); return 1"});
    provider->sources.emplace("B",
                              lunaris::ModuleSource{"game:B", "game://B.luau",
                                                    "require('A'); return 2"});
    auto loader = std::make_shared<lunaris::ModuleLoader>(provider);
    lunaris::State state;
    state.set_module_loader(loader);

    auto requester = state.load_script("return require('A')", "cycle.luau");
    ASSERT_TRUE(requester);
    auto first = requester.value().execute();
    ASSERT_FALSE(first);
    EXPECT_NE(first.error().message().find("cyclic require"),
              std::string_view::npos);

    provider->sources["A"].source = "return 42";
    auto retry = requester.value().execute();
    ASSERT_TRUE(retry) << retry.error().message();
    EXPECT_EQ(std::get<double>(retry.value()[0]), 42.0);
}

TEST(ModuleRequireTests, DisablingLoaderRemovesRequireAndClearsModuleCache) {
    auto provider = std::make_shared<SourceProvider>();
    provider->sources.emplace(
        "Value",
        lunaris::ModuleSource{"game:Value", "game://Value.luau", "return 7"});
    auto loader = std::make_shared<lunaris::ModuleLoader>(provider);
    lunaris::State state;
    state.set_module_loader(loader);
    auto loaded = state.load_script("return require('Value')", "value.luau");
    ASSERT_TRUE(loaded);
    auto first = loaded.value().execute();
    ASSERT_TRUE(first);
    EXPECT_EQ(std::get<double>(first.value()[0]), 7.0);

    state.set_module_loader(nullptr);
    auto disabled = loaded.value().execute();
    ASSERT_FALSE(disabled);
    EXPECT_EQ(disabled.error().type(), lunaris::ErrorType::Runtime);
}
