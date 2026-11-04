#include "lunaris/BytecodeCache.h"

#include <Luau/Bytecode.h>

#include <string>
#include <utility>

namespace lunaris {
namespace {

constexpr std::uint64_t kCompilerCacheVersion = 1;
constexpr std::uint64_t kBytecodeCacheVersion =
    (static_cast<std::uint64_t>(LBC_VERSION_TARGET) << 32) |
    static_cast<std::uint64_t>(LBC_TYPE_VERSION_TARGET);

std::uint64_t source_hash(std::string_view source) {
    constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offsetBasis;
    for (const unsigned char byte : source) {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

Error cache_error(const Error &error) {
    return Error(ErrorType::Cache, std::string(error.message()),
                 std::string(error.source()), error.line(), error.column());
}

} // namespace

std::string BytecodeCache::key_for(std::string_view source) {
    return std::to_string(kCompilerCacheVersion) + "-" +
           std::to_string(kBytecodeCacheVersion) + "-" +
           std::to_string(source.size()) + "-" +
           std::to_string(source_hash(source));
}

Result<Bytecode> BytecodeCache::compile(std::string_view source,
                                        std::string_view sourceIdentifier,
                                        BytecodeCacheStorage &storage) {
    const std::string key = key_for(source);
    auto cached = storage.load(key);
    if (!cached) {
        return Result<Bytecode>::failure(cache_error(cached.error()));
    }

    if (cached.value().has_value()) {
        const BytecodeCacheEntry &entry = *cached.value();
        if (entry.source == source &&
            entry.compilerVersion == kCompilerCacheVersion &&
            entry.bytecodeVersion == kBytecodeCacheVersion) {
            return Result<Bytecode>::success(entry.bytecode);
        }

        auto removed = storage.remove(key);
        if (!removed) {
            return Result<Bytecode>::failure(cache_error(removed.error()));
        }
    }

    auto compiled = Bytecode::compile(source, sourceIdentifier);
    if (!compiled) {
        return compiled;
    }

    BytecodeCacheEntry entry{compiled.value(), std::string(source),
                             std::string(sourceIdentifier),
                             kCompilerCacheVersion, kBytecodeCacheVersion};
    auto stored = storage.store(key, entry);
    if (!stored) {
        return Result<Bytecode>::failure(cache_error(stored.error()));
    }
    return compiled;
}

} // namespace lunaris
