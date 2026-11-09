#include <gtest/gtest.h>
#include <lunaris/state.h>

#include <lua.h>

#include <array>
#include <stdexcept>

namespace {
using lunaris::Library;
using lunaris::State;

// Type of a global variable (LUA_TNIL if it does not exist). Leaves the stack
// as it was.
int globalType(State &state, const char *name) {
    lua_State *L = state.luaState();
    const int type = lua_getglobal(L, name);
    lua_pop(L, 1);
    return type;
}

struct LibraryCase {
    Library library;
    const char *global; // A global that exists once the library is open.
    int type;           // Its expected type.
};

// One entry per Library. The global names are the ones Luau registers.
constexpr std::array libraryCases{
    LibraryCase{Library::base, "print", LUA_TFUNCTION},
    LibraryCase{Library::coroutine, "coroutine", LUA_TTABLE},
    LibraryCase{Library::table, "table", LUA_TTABLE},
    LibraryCase{Library::os, "os", LUA_TTABLE},
    LibraryCase{Library::string, "string", LUA_TTABLE},
    LibraryCase{Library::math, "math", LUA_TTABLE},
    LibraryCase{Library::debug, "debug", LUA_TTABLE},
    LibraryCase{Library::utf8, "utf8", LUA_TTABLE},
    LibraryCase{Library::bit32, "bit32", LUA_TTABLE},
    LibraryCase{Library::buffer, "buffer", LUA_TTABLE},
    LibraryCase{Library::vector, "vector", LUA_TTABLE},
};
} // namespace

// --- Contract checked at compile time ---------------------------------------

namespace {
// Whether State::openLibraries() can be called with arguments of these types.
// Must be a template: outside of one, an invalid call would be a hard error
// instead of "false".
template <typename... Args>
concept CanOpenLibraries =
    requires(State &state, Args... args) { state.openLibraries(args...); };
} // namespace

static_assert(CanOpenLibraries<Library>);
static_assert(CanOpenLibraries<Library, Library, Library>);
static_assert(!CanOpenLibraries<>);
static_assert(!CanOpenLibraries<int>);
static_assert(!CanOpenLibraries<Library, int>);

// --- Nothing is open by default ----------------------------------------------

TEST(LibrariesTest, FreshStateHasNoLibraries) {
    State state;
    for (const LibraryCase &c : libraryCases) {
        SCOPED_TRACE(c.global);
        EXPECT_EQ(globalType(state, c.global), LUA_TNIL);
    }
}

// --- Opening one library
// ------------------------------------------------------

TEST(LibrariesTest, EveryLibraryOpensIndividually) {
    for (const LibraryCase &c : libraryCases) {
        SCOPED_TRACE(c.global);
        State state;

        state.openLibraries(c.library);

        EXPECT_EQ(globalType(state, c.global), c.type);
        EXPECT_EQ(lua_gettop(state.luaState()), 0);
    }
}

TEST(LibrariesTest, OpensOnlyTheRequestedLibrary) {
    State state;

    state.openLibraries(Library::math);

    EXPECT_EQ(globalType(state, "math"), LUA_TTABLE);
    EXPECT_EQ(globalType(state, "string"), LUA_TNIL);
    EXPECT_EQ(globalType(state, "print"), LUA_TNIL);
}

// --- Opening several libraries
// --------------------------------------------------

TEST(LibrariesTest, OpensSeveralLibraries) {
    State state;

    state.openLibraries(Library::base, Library::math, Library::string);

    EXPECT_EQ(globalType(state, "print"), LUA_TFUNCTION);
    EXPECT_EQ(globalType(state, "math"), LUA_TTABLE);
    EXPECT_EQ(globalType(state, "string"), LUA_TTABLE);
    EXPECT_EQ(globalType(state, "table"), LUA_TNIL);
}

TEST(LibrariesTest, OrderOfArgumentsDoesNotMatter) {
    State first;
    State second;

    first.openLibraries(Library::math, Library::base);
    second.openLibraries(Library::base, Library::math);

    for (const LibraryCase &c : libraryCases) {
        SCOPED_TRACE(c.global);
        EXPECT_EQ(globalType(first, c.global), globalType(second, c.global));
    }
    EXPECT_EQ(globalType(first, "math"), LUA_TTABLE);
    EXPECT_EQ(globalType(first, "print"), LUA_TFUNCTION);
}

TEST(LibrariesTest, DuplicateArgumentsAreAccepted) {
    State state;

    state.openLibraries(Library::math, Library::math, Library::base);

    EXPECT_EQ(globalType(state, "math"), LUA_TTABLE);
    EXPECT_EQ(globalType(state, "print"), LUA_TFUNCTION);
    EXPECT_EQ(lua_gettop(state.luaState()), 0);
}

// --- All libraries
// ---------------------------------------------------------------

TEST(LibrariesTest, OpenAllLibrariesOpensEveryLibrary) {
    State state;

    state.openAllLibraries();

    for (const LibraryCase &c : libraryCases) {
        SCOPED_TRACE(c.global);
        EXPECT_EQ(globalType(state, c.global), c.type);
    }
    EXPECT_EQ(lua_gettop(state.luaState()), 0);
}

// --- Stack and repeated calls
// ----------------------------------------------------

TEST(LibrariesTest, KeepsExistingStackValues) {
    State state;
    lua_State *L = state.luaState();
    lua_pushnumber(L, 7.0);

    state.openLibraries(Library::base, Library::math);

    ASSERT_EQ(lua_gettop(L), 1);
    EXPECT_EQ(lua_tonumber(L, 1), 7.0);
}

TEST(LibrariesTest, CanBeCalledAgain) {
    State state;

    state.openLibraries(Library::math);
    state.openLibraries(Library::math, Library::string);

    EXPECT_EQ(globalType(state, "math"), LUA_TTABLE);
    EXPECT_EQ(globalType(state, "string"), LUA_TTABLE);
    EXPECT_EQ(lua_gettop(state.luaState()), 0);
}

// --- Invalid input
// -----------------------------------------------------------------

TEST(LibrariesTest, InvalidLibraryValueThrowsAndOpensNothing) {
    State state;
    const auto invalid = static_cast<Library>(200);

    EXPECT_THROW(state.openLibraries(Library::math, invalid),
                 std::invalid_argument);

    EXPECT_EQ(globalType(state, "math"), LUA_TNIL);
    EXPECT_EQ(lua_gettop(state.luaState()), 0);
}
