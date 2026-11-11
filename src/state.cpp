#include "lunaris/state.h"
#include "lunaris/detail/state_context.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

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
    m_context = std::move(other.m_context);
    other.m_context->close();
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
} // namespace lunaris
