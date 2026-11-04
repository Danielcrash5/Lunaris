#include "lunaris/LuaRocksProvider.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("lunaris-luarocks-" + std::to_string(suffix));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return m_path;
    }

    void write(std::string_view relativePath, std::string_view contents) const {
        const auto filePath = m_path / relativePath;
        std::filesystem::create_directories(filePath.parent_path());
        std::ofstream output(filePath, std::ios::binary);
        ASSERT_TRUE(output) << "could not create fixture " << filePath.string();
        output.write(contents.data(),
                     static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(output) << "could not write fixture " << filePath.string();
    }

  private:
    std::filesystem::path m_path;
};

} // namespace

TEST(LuaRocksProviderTests, ResolvesDottedLuaModulesAndModuleInitFiles) {
    TemporaryDirectory root;
    root.write("sample/math.lua", "return 42");
    root.write("sample/nested/init.luau", "return 17");
    lunaris::LuaRocksModuleProvider provider;

    auto math = provider.resolve("sample.math", root.path().string());
    auto nested = provider.resolve("sample.nested", root.path().string());

    ASSERT_TRUE(math) << math.error().message();
    ASSERT_TRUE(math.value().has_value());
    EXPECT_EQ(math.value()->source, "return 42");
    EXPECT_EQ(math.value()->canonicalId,
              "luarocks:" +
                  std::filesystem::canonical(root.path() / "sample/math.lua")
                      .string());
    ASSERT_TRUE(nested) << nested.error().message();
    ASSERT_TRUE(nested.value().has_value());
    EXPECT_EQ(nested.value()->source, "return 17");
}

TEST(LuaRocksProviderTests, RejectsInvalidNamesAndDoesNotResolveNativeModules) {
    TemporaryDirectory root;
    root.write("outside.lua", "return 42");
    root.write("native.dll", "not a loadable Lunaris module");
    lunaris::LuaRocksModuleProvider provider;

    auto traversal = provider.resolve("../outside", root.path().string());
    auto native = provider.resolve("native", root.path().string());
    auto missing = provider.resolve("missing", root.path().string());

    EXPECT_FALSE(traversal);
    ASSERT_FALSE(native);
    EXPECT_NE(native.error().message().find("native Lua modules are not "
                                            "supported"),
              std::string_view::npos);
    ASSERT_TRUE(missing);
    EXPECT_FALSE(missing.value().has_value());
}

TEST(LuaRocksProviderTests, LoadsSourcePackagesThroughLuauRequire) {
    TemporaryDirectory root;
    root.write("rocks/sample.lua", "return { value = 42 }");
    auto provider = std::make_shared<lunaris::LuaRocksModuleProvider>();
    lunaris::ModuleLoaderConfig config;
    config.basePath = root.path().string();
    auto loader =
        std::make_shared<lunaris::ModuleLoader>(provider, std::move(config));
    lunaris::State state;
    state.set_module_loader(std::move(loader));

    auto script = state.load_script("return require('rocks.sample').value",
                                    "luarocks-source-module.luau");
    ASSERT_TRUE(script);
    auto result = script.value().execute();
    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_EQ(std::get<double>(result.value()[0]), 42.0);
}
