#pragma once

#include "lunaris/Bytecode.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lunaris {

/// Versioned bytecode metadata stored by a BytecodeCacheStorage.
struct BytecodeCacheEntry {
    Bytecode bytecode;
    std::string source;
    std::string sourceIdentifier;
    std::uint64_t compilerVersion;
    std::uint64_t bytecodeVersion;
    CompilerOptions compilerOptions;
};

/// Pluggable storage for compiled bytecode; implementations choose the backing
/// store and must return storage failures as Cache errors.
class BytecodeCacheStorage {
  public:
    virtual ~BytecodeCacheStorage() = default;

    /// Loads an entry, returning an empty optional when the key is not present.
    [[nodiscard]] virtual Result<std::optional<BytecodeCacheEntry>>
    load(std::string_view key) = 0;

    /// Stores or replaces an entry for the key.
    [[nodiscard]] virtual Result<void>
    store(std::string_view key, const BytecodeCacheEntry &entry) = 0;

    /// Removes an entry for the key; removing a missing key succeeds.
    [[nodiscard]] virtual Result<void> remove(std::string_view key) = 0;
};

/// Compiles source with a caller-provided cache storage.
class BytecodeCache {
  public:
    /// Returns cached bytecode when its source and version metadata match,
    /// otherwise compiles and stores a fresh entry.
    /// @return Bytecode or a Compile, Memory, or Cache error.
    [[nodiscard]] static Result<Bytecode>
    compile(std::string_view source, std::string_view sourceIdentifier,
            BytecodeCacheStorage &storage, const CompilerOptions &options = {});

    /// Returns a stable storage key derived from the source and compiler
    /// versions used by this Lunaris build.
    [[nodiscard]] static std::string
    key_for(std::string_view source, const CompilerOptions &options = {});
};

} // namespace lunaris
