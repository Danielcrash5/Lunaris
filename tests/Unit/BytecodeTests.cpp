#include "lunaris/Bytecode.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>

TEST(BytecodeTests, CompilesSourceIntoOwnedVersionedBytecode) {
    auto bytecode =
        lunaris::Bytecode::compile("return 17", "game://scripts/value.luau");

    ASSERT_TRUE(bytecode);
    EXPECT_FALSE(bytecode.value().bytes().empty());
    EXPECT_GE(bytecode.value().version(), 3);
    EXPECT_LE(bytecode.value().version(), 14);
    EXPECT_EQ(bytecode.value().type_version(), 3);
}

TEST(BytecodeTests, ReportsCompileErrorsWithSourceLocation) {
    auto result = lunaris::Bytecode::compile("local = ", "broken-source.luau");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Compile);
    EXPECT_EQ(result.error().source(), "broken-source.luau");
    EXPECT_EQ(result.error().line(), 1);
    EXPECT_GE(result.error().column(), 1);
}

TEST(BytecodeTests, ValidatesAndAppliesCompilerOptions) {
    lunaris::CompilerOptions options;
    options.debugLevel = 2;
    auto bytecode = lunaris::Bytecode::compile("local value = 17; return value",
                                               {}, options);
    ASSERT_TRUE(bytecode);

    options.optimizationLevel = 3;
    auto invalid = lunaris::Bytecode::compile("return 17",
                                              "invalid-options.luau", options);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().type(), lunaris::ErrorType::Compile);
    EXPECT_EQ(invalid.error().source(), "invalid-options.luau");

    lunaris::State state;
    EXPECT_THROW(state.set_compiler_options(options), std::invalid_argument);
}

TEST(BytecodeTests, LoadsOwnedBytecodeIntoState) {
    auto bytecode = lunaris::Bytecode::compile("return 17");
    ASSERT_TRUE(bytecode);

    lunaris::State state;
    auto loaded = state.load_bytecode(bytecode.value(), "precompiled.luau");

    ASSERT_TRUE(loaded);
    auto execution = loaded.value().execute();
    ASSERT_TRUE(execution);
    ASSERT_EQ(execution.value().size(), 1U);
    EXPECT_EQ(std::get<double>(execution.value()[0]), 17.0);
    EXPECT_EQ(state.top(), 0);
}

TEST(BytecodeTests, ReconstructsBytecodeFromOwnedBytes) {
    auto compiled = lunaris::Bytecode::compile("return true");
    ASSERT_TRUE(compiled);

    auto reconstructed =
        lunaris::Bytecode::from_bytes(compiled.value().bytes());

    ASSERT_TRUE(reconstructed);
    EXPECT_EQ(reconstructed.value().bytes(), compiled.value().bytes());
    EXPECT_EQ(reconstructed.value().version(), compiled.value().version());
    EXPECT_EQ(reconstructed.value().type_version(),
              compiled.value().type_version());
}

TEST(BytecodeTests, RejectsEmptyOrUnsupportedBytecodeHeaders) {
    auto empty = lunaris::Bytecode::from_bytes({});
    std::string unsupportedVersion(1, '\0');
    unsupportedVersion[0] = 1;
    auto unsupported = lunaris::Bytecode::from_bytes(unsupportedVersion);
    std::string unsupportedTypeVersion(2, '\0');
    unsupportedTypeVersion[0] = 4;
    auto unsupportedType =
        lunaris::Bytecode::from_bytes(unsupportedTypeVersion);

    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().type(), lunaris::ErrorType::Load);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().type(), lunaris::ErrorType::Load);
    ASSERT_FALSE(unsupportedType);
    EXPECT_EQ(unsupportedType.error().type(), lunaris::ErrorType::Load);
}
