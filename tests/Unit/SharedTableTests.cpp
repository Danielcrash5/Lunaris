#include "lunaris/SharedTable.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

TEST(SharedTableTests, SharesTablesAcrossInstalledStates) {
    lunaris::SharedTableRegistry registry;
    lunaris::State firstState;
    lunaris::State secondState;
    registry.install(firstState);
    registry.install(secondState);

    auto publish = firstState.load_script(
        "local table = SharedTable.new(); table.value = 42; "
        "SharedTableRegistry:SetSharedTable('world', table); return "
        "table.value",
        "shared-table-publish.luau");
    ASSERT_TRUE(publish);
    auto published = publish.value().execute();
    ASSERT_TRUE(published) << published.error().message();
    ASSERT_EQ(published.value().size(), 1U);
    EXPECT_EQ(std::get<double>(published.value()[0]), 42.0);

    auto read = secondState.load_script(
        "local table = SharedTableRegistry:GetSharedTable('world'); "
        "return table.value, table:Count()",
        "shared-table-read.luau");
    ASSERT_TRUE(read);
    auto result = read.value().execute();
    ASSERT_TRUE(result) << result.error().message();
    ASSERT_EQ(result.value().size(), 2U);
    EXPECT_EQ(std::get<double>(result.value()[0]), 42.0);
    EXPECT_EQ(std::get<std::int64_t>(result.value()[1]), 1);

    auto remove = firstState.load_script(
        "SharedTableRegistry:SetSharedTable('world', nil)",
        "shared-table-remove.luau");
    ASSERT_TRUE(remove);
    ASSERT_TRUE(remove.value().execute());

    auto missing = secondState.load_script(
        "return SharedTableRegistry:GetSharedTable('world')",
        "shared-table-missing.luau");
    ASSERT_TRUE(missing);
    auto missingResult = missing.value().execute();
    ASSERT_TRUE(missingResult) << missingResult.error().message();
    ASSERT_EQ(missingResult.value().size(), 1U);
    EXPECT_TRUE(
        std::holds_alternative<std::monostate>(missingResult.value()[0]));
}

TEST(SharedTableTests, SupportsNestedTablesAndRegistryRemoval) {
    lunaris::SharedTableRegistry registry;
    auto root = registry.create_table();
    auto child = registry.create_table();
    child.set("name", lunaris::Value{"nested"});
    root.set("child", child);
    root.set("temporary", lunaris::Value{std::int64_t{7}});

    registry.set_shared_table("root", root);
    auto readRoot = registry.get_shared_table("root");
    ASSERT_TRUE(readRoot);
    ASSERT_EQ(readRoot->size(), 2U);
    auto readChild = readRoot->get_table("child");
    ASSERT_TRUE(readChild);
    ASSERT_EQ(readChild->get("name"), lunaris::Value{"nested"});
    EXPECT_THROW((void)readRoot->get("child"), std::invalid_argument);
    EXPECT_TRUE(readRoot->remove("temporary"));
    EXPECT_FALSE(readRoot->get("temporary"));

    registry.set_shared_table("root", std::nullopt);
    EXPECT_FALSE(registry.get_shared_table("root"));
}

TEST(SharedTableTests, RejectsUnsupportedLuauValuesAndUsesNilToRemove) {
    lunaris::SharedTableRegistry registry;
    lunaris::State state;
    registry.install(state);

    auto invalidValue = state.load_script(
        "local table = SharedTable.new(); table.value = {}; return true",
        "shared-table-invalid-value.luau");
    ASSERT_TRUE(invalidValue);
    auto invalidValueResult = invalidValue.value().execute();
    ASSERT_FALSE(invalidValueResult);
    EXPECT_EQ(invalidValueResult.error().type(), lunaris::ErrorType::Runtime);

    auto invalidKey =
        state.load_script("local table = SharedTable.new(); table[1] = 'value'",
                          "shared-table-invalid-key.luau");
    ASSERT_TRUE(invalidKey);
    EXPECT_FALSE(invalidKey.value().execute());

    auto removeWithNil = state.load_script(
        "local table = SharedTable.new(); table.value = 8; "
        "table.value = nil; return table.value, table:Count()",
        "shared-table-nil-removal.luau");
    ASSERT_TRUE(removeWithNil);
    auto removed = removeWithNil.value().execute();
    ASSERT_TRUE(removed) << removed.error().message();
    ASSERT_EQ(removed.value().size(), 2U);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(removed.value()[0]));
    EXPECT_EQ(std::get<std::int64_t>(removed.value()[1]), 0);
}

TEST(SharedTableTests, OperationsAreSafeAcrossHostThreads) {
    lunaris::SharedTableRegistry registry;
    lunaris::SharedTable table = registry.create_table();
    constexpr int threadCount = 4;
    constexpr int valuesPerThread = 250;
    std::atomic<bool> valuesMatched = true;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int thread = 0; thread < threadCount; ++thread) {
        threads.emplace_back([table, thread, &valuesMatched] {
            for (int value = 0; value < valuesPerThread; ++value) {
                const std::string key =
                    std::to_string(thread) + ":" + std::to_string(value);
                table.set(key,
                          lunaris::Value{static_cast<std::int64_t>(value)});
                const auto stored = table.get(key);
                if (!stored.has_value() ||
                    !std::holds_alternative<std::int64_t>(*stored) ||
                    std::get<std::int64_t>(*stored) != value) {
                    valuesMatched = false;
                }
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(table.size(), threadCount * valuesPerThread);
    EXPECT_TRUE(valuesMatched);
}
