#include "lunaris/BytecodeCache.h"
#include "lunaris/State.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace {

class MemoryBytecodeCache final : public lunaris::BytecodeCacheStorage {
  public:
    lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>
    load(std::string_view key) override {
        ++loadCount;
        const auto iterator = entries.find(std::string(key));
        if (iterator == entries.end()) {
            return lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>::
                success(std::nullopt);
        }
        return lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>::
            success(iterator->second);
    }

    lunaris::Result<void>
    store(std::string_view key,
          const lunaris::BytecodeCacheEntry &entry) override {
        ++storeCount;
        entries.insert_or_assign(std::string(key), entry);
        return lunaris::Result<void>::success();
    }

    lunaris::Result<void> remove(std::string_view key) override {
        ++removeCount;
        entries.erase(std::string(key));
        return lunaris::Result<void>::success();
    }

    std::unordered_map<std::string, lunaris::BytecodeCacheEntry> entries;
    int loadCount = 0;
    int storeCount = 0;
    int removeCount = 0;
};

class FailingBytecodeCache final : public lunaris::BytecodeCacheStorage {
  public:
    enum class Operation { Load, Store, Remove };

    explicit FailingBytecodeCache(Operation operation)
        : m_operation(operation) {
    }

    lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>
    load(std::string_view key) override {
        if (m_operation == Operation::Load) {
            return lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>::
                failure(lunaris::Error(lunaris::ErrorType::Cache,
                                       "storage unavailable"));
        }

        const auto iterator = m_entries.find(std::string(key));
        if (iterator == m_entries.end()) {
            return lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>::
                success(std::nullopt);
        }
        return lunaris::Result<std::optional<lunaris::BytecodeCacheEntry>>::
            success(iterator->second);
    }

    lunaris::Result<void> store(std::string_view,
                                const lunaris::BytecodeCacheEntry &) override {
        return m_operation == Operation::Store
                   ? failure()
                   : lunaris::Result<void>::success();
    }

    lunaris::Result<void> remove(std::string_view key) override {
        if (m_operation == Operation::Remove) {
            return failure();
        }
        m_entries.erase(std::string(key));
        return lunaris::Result<void>::success();
    }

    void seed(std::string key, lunaris::BytecodeCacheEntry entry) {
        m_entries.insert_or_assign(std::move(key), std::move(entry));
    }

  private:
    static lunaris::Result<void> failure() {
        return lunaris::Result<void>::failure(
            lunaris::Error(lunaris::ErrorType::Cache, "storage unavailable"));
    }

    Operation m_operation;
    std::unordered_map<std::string, lunaris::BytecodeCacheEntry> m_entries;
};

} // namespace

TEST(BytecodeCacheTests, CompilesOnMissAndReusesMatchingEntry) {
    MemoryBytecodeCache storage;

    auto first =
        lunaris::BytecodeCache::compile("return 23", "first.luau", storage);
    auto second =
        lunaris::BytecodeCache::compile("return 23", "second.luau", storage);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.value().bytes(), second.value().bytes());
    EXPECT_EQ(storage.loadCount, 2);
    EXPECT_EQ(storage.storeCount, 1);
    EXPECT_EQ(storage.removeCount, 0);

    lunaris::State state;
    auto loaded = state.load_bytecode(second.value(), "cache-hit.luau");
    ASSERT_TRUE(loaded);
    auto execution = loaded.value().execute();
    ASSERT_TRUE(execution);
    EXPECT_EQ(std::get<double>(execution.value()[0]), 23.0);
}

TEST(BytecodeCacheTests, RebuildsEntryWhoseSourceDoesNotMatchItsKey) {
    MemoryBytecodeCache storage;
    auto otherBytecode = lunaris::Bytecode::compile("return 99");
    ASSERT_TRUE(otherBytecode);
    const std::string source = "return 23";
    const std::string key = lunaris::BytecodeCache::key_for(source);
    storage.entries.emplace(
        key, lunaris::BytecodeCacheEntry{std::move(otherBytecode).value(),
                                         "different source", "old.luau", 1, 0});

    auto result =
        lunaris::BytecodeCache::compile(source, "fresh.luau", storage);

    ASSERT_TRUE(result);
    EXPECT_EQ(storage.removeCount, 1);
    EXPECT_EQ(storage.storeCount, 1);
    lunaris::State state;
    auto loaded = state.load_bytecode(result.value(), "fresh.luau");
    ASSERT_TRUE(loaded);
    auto execution = loaded.value().execute();
    ASSERT_TRUE(execution);
    EXPECT_EQ(std::get<double>(execution.value()[0]), 23.0);
}

TEST(BytecodeCacheTests, ReturnsCompileErrorsWithoutStoringThem) {
    MemoryBytecodeCache storage;

    auto result =
        lunaris::BytecodeCache::compile("local = ", "broken.luau", storage);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Compile);
    EXPECT_EQ(result.error().source(), "broken.luau");
    EXPECT_EQ(storage.storeCount, 0);
}

TEST(BytecodeCacheTests, SurfacesStorageFailuresAsCacheErrors) {
    FailingBytecodeCache storage(FailingBytecodeCache::Operation::Load);

    auto result =
        lunaris::BytecodeCache::compile("return 1", "script.luau", storage);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().type(), lunaris::ErrorType::Cache);
    EXPECT_EQ(result.error().message(), "storage unavailable");
}

TEST(BytecodeCacheTests, SurfacesStoreAndRemovalFailures) {
    FailingBytecodeCache store(FailingBytecodeCache::Operation::Store);
    auto storeResult =
        lunaris::BytecodeCache::compile("return 1", "script.luau", store);
    ASSERT_FALSE(storeResult);
    EXPECT_EQ(storeResult.error().type(), lunaris::ErrorType::Cache);

    FailingBytecodeCache remove(FailingBytecodeCache::Operation::Remove);
    auto staleBytecode = lunaris::Bytecode::compile("return 2");
    ASSERT_TRUE(staleBytecode);
    const std::string source = "return 1";
    remove.seed(lunaris::BytecodeCache::key_for(source),
                lunaris::BytecodeCacheEntry{std::move(staleBytecode).value(),
                                            "stale source", "old.luau", 1, 0});
    auto removeResult =
        lunaris::BytecodeCache::compile(source, "script.luau", remove);
    ASSERT_FALSE(removeResult);
    EXPECT_EQ(removeResult.error().type(), lunaris::ErrorType::Cache);
}

TEST(BytecodeCacheTests, KeyIncludesSourceContent) {
    EXPECT_NE(lunaris::BytecodeCache::key_for("return 1"),
              lunaris::BytecodeCache::key_for("return 2"));
}
