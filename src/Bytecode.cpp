#include "lunaris/Bytecode.h"

#include <Luau/Allocator.h>
#include <Luau/Bytecode.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/Compiler.h>
#include <Luau/Parser.h>

#include <exception>
#include <new>
#include <string>

namespace lunaris {
namespace {

std::uint8_t byte_at(std::string_view bytes, std::size_t index) {
    return static_cast<std::uint8_t>(static_cast<unsigned char>(bytes[index]));
}

Result<Bytecode> invalid_header(std::string message) {
    return Result<Bytecode>::failure(
        Error(ErrorType::Load, std::move(message)));
}

} // namespace

Bytecode::Bytecode(std::string bytes, std::uint8_t version,
                   std::uint8_t typeVersion)
    : m_bytes(std::move(bytes)), m_version(version),
      m_typeVersion(typeVersion) {
}

Result<Bytecode> Bytecode::compile(std::string_view source,
                                   std::string_view sourceName) {
    const std::string sourceText(source);
    const std::string sourceIdentifier(sourceName);
    try {
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::ParseResult parseResult = Luau::Parser::parse(
            sourceText.c_str(), sourceText.size(), names, allocator);
        if (!parseResult.errors.empty()) {
            const Luau::ParseError &parseError = parseResult.errors.front();
            const Luau::Location &location = parseError.getLocation();
            return Result<Bytecode>::failure(
                Error(ErrorType::Compile, parseError.what(), sourceIdentifier,
                      location.begin.line + 1, location.begin.column + 1));
        }

        Luau::BytecodeBuilder bytecodeBuilder;
        Luau::compileOrThrow(bytecodeBuilder, parseResult, names);
        std::string bytecode = bytecodeBuilder.getBytecode();
        return from_bytes(bytecode);
    } catch (const Luau::CompileError &exception) {
        const Luau::Location &location = exception.getLocation();
        return Result<Bytecode>::failure(
            Error(ErrorType::Compile, exception.what(), sourceIdentifier,
                  location.begin.line + 1, location.begin.column + 1));
    } catch (const std::bad_alloc &) {
        return Result<Bytecode>::failure(
            Error(ErrorType::Memory, "out of memory while compiling bytecode",
                  sourceIdentifier));
    } catch (const std::exception &exception) {
        return Result<Bytecode>::failure(
            Error(ErrorType::Compile, exception.what(), sourceIdentifier));
    }
}

Result<Bytecode> Bytecode::from_bytes(std::string_view bytes) {
    if (bytes.empty()) {
        return invalid_header("bytecode is empty");
    }

    const std::uint8_t version = byte_at(bytes, 0);
    if (version == 0) {
        return invalid_header("bytecode contains an encoded compiler error");
    }

    if ((version < LBC_VERSION_MIN || version > LBC_VERSION_MAX) &&
        version != LBC_VERSION_CLASSES) {
        return invalid_header("unsupported Luau bytecode version " +
                              std::to_string(version));
    }

    std::uint8_t typeVersion = 0;
    if (version >= 4) {
        if (bytes.size() < 2) {
            return invalid_header(
                "bytecode is missing its type information version");
        }
        typeVersion = byte_at(bytes, 1);
        if (typeVersion < LBC_TYPE_VERSION_MIN ||
            typeVersion > LBC_TYPE_VERSION_MAX) {
            return invalid_header("unsupported Luau type information version " +
                                  std::to_string(typeVersion));
        }
    }

    return Result<Bytecode>::success(
        Bytecode(std::string(bytes), version, typeVersion));
}

std::string_view Bytecode::bytes() const noexcept {
    return m_bytes;
}

std::uint8_t Bytecode::version() const noexcept {
    return m_version;
}

std::uint8_t Bytecode::type_version() const noexcept {
    return m_typeVersion;
}

} // namespace lunaris
