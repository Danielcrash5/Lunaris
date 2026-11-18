#include "lunaris/state.h"
#include "lunaris/detail/state_context.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

namespace lunaris {
namespace {
struct LibraryEntry {
    Library library;
    const char *name; // Name passed to the open function, "" for base.
    lua_CFunction open;
};

// The position of an entry must match the value of its Library, because the
// position is used as the bit in the selection mask.
constexpr std::array libraryTable{
    LibraryEntry{Library::base, "", luaopen_base},
    LibraryEntry{Library::coroutine, LUA_COLIBNAME, luaopen_coroutine},
    LibraryEntry{Library::table, LUA_TABLIBNAME, luaopen_table},
    LibraryEntry{Library::os, LUA_OSLIBNAME, luaopen_os},
    LibraryEntry{Library::string, LUA_STRLIBNAME, luaopen_string},
    LibraryEntry{Library::math, LUA_MATHLIBNAME, luaopen_math},
    LibraryEntry{Library::debug, LUA_DBLIBNAME, luaopen_debug},
    LibraryEntry{Library::utf8, LUA_UTF8LIBNAME, luaopen_utf8},
    LibraryEntry{Library::bit32, LUA_BITLIBNAME, luaopen_bit32},
    LibraryEntry{Library::buffer, LUA_BUFFERLIBNAME, luaopen_buffer},
    LibraryEntry{Library::vector, LUA_VECLIBNAME, luaopen_vector},
};

constexpr bool tableMatchesEnumeration() {
    for (std::size_t i = 0; i < libraryTable.size(); ++i) {
        if (std::to_underlying(libraryTable[i].library) != i) {
            return false;
        }
    }
    return true;
}
static_assert(tableMatchesEnumeration(),
              "libraryTable must list the libraries in enumeration order");

constexpr std::uint16_t allLibrariesMask =
    static_cast<std::uint16_t>((1U << libraryTable.size()) - 1U);

// Opens one library the same way luaL_openlibs does: the open function is
// called with the name of the library as its argument.
void openLibrary(lua_State *state, const LibraryEntry &entry) {
    lua_pushcfunction(state, entry.open, nullptr);
    lua_pushstring(state, entry.name);
    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
        // Opening a library only fails when memory runs out.
        lua_pop(state, 1); // the error value
        throw std::bad_alloc();
    }
}

void openByMask(lua_State *state, std::uint16_t mask) {
    const int top = lua_gettop(state);
    try {
        for (std::size_t i = 0; i < libraryTable.size(); ++i) {
            if ((mask & (1U << i)) != 0U) {
                openLibrary(state, libraryTable[i]);
            }
        }
    } catch (...) {
        lua_settop(state, top);
        throw;
    }
}

std::string errorValueMessage(lua_State *state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TSTRING:
    case LUA_TNUMBER: {
        std::size_t length = 0;
        const char *value = lua_tolstring(state, index, &length);
        return value == nullptr ? std::string{}
                                : std::string(value, length);
    }
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0 ? "true" : "false";
    default:
        return std::string("error object of type ") +
               luaL_typename(state, index);
    }
}

std::optional<std::uint32_t>
compileLineNumber(std::string_view message, std::string_view chunkName) {
    // Luau truncates displayed chunk names to LUA_IDSIZE - 1 characters.
    const std::string_view displayedName =
        chunkName.size() < LUA_IDSIZE ? chunkName
                                     : chunkName.substr(0, LUA_IDSIZE - 1);
    if (!message.starts_with(displayedName) ||
        message.size() <= displayedName.size() ||
        message[displayedName.size()] != ':') {
        return std::nullopt;
    }

    const std::size_t lineStart = displayedName.size() + 1;
    std::size_t lineEnd = lineStart;
    while (lineEnd < message.size() && message[lineEnd] >= '0' &&
           message[lineEnd] <= '9') {
        ++lineEnd;
    }
    if (lineEnd == lineStart || lineEnd >= message.size() ||
        message[lineEnd] != ':') {
        return std::nullopt;
    }

    std::uint32_t line = 0;
    const auto [parsedEnd, error] =
        std::from_chars(message.data() + lineStart,
                        message.data() + lineEnd, line);
    if (error != std::errc{} || parsedEnd != message.data() + lineEnd) {
        return std::nullopt;
    }
    return line;
}

struct RuntimeErrorCapture {
    ErrorDetails details;
    bool captured = false;
};

int runtimeErrorHandler(lua_State *state) {
    auto *capture = static_cast<RuntimeErrorCapture *>(
        lua_touserdata(state, lua_upvalueindex(1)));
    if (capture != nullptr) {
        capture->details.kind = ErrorKind::runtime;
        capture->details.message = errorValueMessage(state, 1);

        int tracebackStartLevel = 1;
        for (int level = 1;; ++level) {
            lua_Debug frame{};
            if (lua_getinfo(state, level, "sl", &frame) == 0) {
                break;
            }
            if (frame.what != nullptr && std::strcmp(frame.what, "C") != 0) {
                if (frame.source != nullptr) {
                    std::string_view sourceName(frame.source);
                    if (sourceName.starts_with('=')) {
                        sourceName.remove_prefix(1);
                    }
                    capture->details.chunkName.assign(sourceName);
                }
                if (frame.currentline > 0) {
                    capture->details.lineNumber =
                        static_cast<std::uint32_t>(frame.currentline);
                }
                tracebackStartLevel = level;
                break;
            }
        }

        luaL_traceback(state, state, nullptr, tracebackStartLevel);
        std::size_t tracebackLength = 0;
        const char *traceback = lua_tolstring(state, -1, &tracebackLength);
        if (traceback != nullptr) {
            capture->details.traceback.assign(traceback, tracebackLength);
        }
        lua_pop(state, 1);
        capture->captured = true;
    }

    lua_pushvalue(state, 1);
    return 1;
}
} // namespace

State::State() : m_context(std::make_shared<detail::StateContext>()) {
    if (!m_context->luaState()) {
        throw std::bad_alloc();
    }
}

State::~State() {
    if (m_context) {
        m_context->close();
    }
}

State &State::operator=(State &&other) noexcept {
    if (this != &other) {
        // Close the state this object owned until now, then take over the
        // other.
        if (m_context) {
            m_context->close();
        }
        m_context = std::move(other.m_context);
    }
    return *this;
}

lua_State *State::luaState() const noexcept {
    if (!m_context || !m_context->isOpen()) {
        return nullptr;
    }
    return m_context->luaState();
}

std::shared_ptr<detail::StateContext> State::context() const noexcept {
    if (!m_context) {
        return nullptr;
    }
    return m_context;
}

void State::openLibrariesImpl(std::span<const Library> libraries) {
    // Validate everything first, so that nothing is opened for bad input.
    std::uint16_t mask = 0;
    for (const Library library : libraries) {
        const auto index = std::to_underlying(library);
        if (index >= libraryTable.size()) {
            throw std::invalid_argument("lunaris: invalid Library value");
        }
        mask = static_cast<std::uint16_t>(mask | (1U << index));
    }
    openByMask(m_context->luaState(), mask);
}

void State::openAllLibraries() {
    openByMask(m_context->luaState(), allLibrariesMask);
}

ScriptResult State::script(std::string_view source,
                           std::string_view chunkName) {
    lua_State *L = luaState();
    if (L == nullptr) {
        throw Error(ErrorDetails(ErrorKind::state, "State is closed",
                                 std::string(chunkName), std::nullopt, ""));
    }

    if (chunkName.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("chunkName must not contain a null byte");
    }

    std::string bytecode = Luau::compile(
        std::string(source), Luau::CompileOptions{}, Luau::ParseOptions{});

    std::string luauChunkName = "=";
    luauChunkName.append(chunkName);

    struct StackGuard {
        lua_State *state;
        int top;

        ~StackGuard() noexcept {
            lua_settop(state, top);
        }
    };

    const int stackTopBefore = lua_gettop(L);
    [[maybe_unused]] const StackGuard stackGuard{L, stackTopBefore};

    RuntimeErrorCapture runtimeError;
    runtimeError.details.kind = ErrorKind::runtime;
    runtimeError.details.chunkName = chunkName;
    lua_pushlightuserdata(L, &runtimeError);
    lua_pushcclosure(L, runtimeErrorHandler, "lunaris runtime error", 1);
    const int errorHandlerIndex = lua_absindex(L, -1);

    if (luau_load(L, luauChunkName.c_str(), bytecode.data(), bytecode.size(),
                  LUA_GLOBALSINDEX) != 0) {
        const std::string message = errorValueMessage(L, -1);
        if (message == "not enough memory") {
            throw std::bad_alloc();
        }

        ErrorDetails details;
        details.kind = ErrorKind::compile;
        details.message = message;
        details.chunkName = chunkName;
        details.lineNumber = compileLineNumber(message, chunkName);
        throw Error(details);
    }

    const int callStatus =
        lua_pcall(L, 0, LUA_MULTRET, errorHandlerIndex);
    if (callStatus == LUA_ERRMEM) {
        throw std::bad_alloc();
    }
    if (callStatus != LUA_OK) {
        if (!runtimeError.captured) {
            runtimeError.details.message = errorValueMessage(L, -1);
        }
        throw Error(runtimeError.details);
    }

    lua_remove(L, errorHandlerIndex);

    const int stackTopAfter = lua_gettop(L);
    const int newValuesCount = stackTopAfter - stackTopBefore;

    std::vector<int> references;
    references.reserve(static_cast<std::size_t>(newValuesCount));
    for (int i = 1; i <= newValuesCount; ++i) {
        references.push_back(lua_ref(L, stackTopBefore + i));
    }

    return ScriptResult(m_context, std::move(references));
}

std::expected<ScriptResult, Error>
State::safeScript(std::string_view source, std::string_view chunkName) {
    try {
        return script(source, chunkName);
    } catch (const Error &error) {
        return std::unexpected(error);
    }
}
} // namespace lunaris
