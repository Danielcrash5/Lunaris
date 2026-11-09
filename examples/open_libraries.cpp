// Shows how to open standard libraries in a State. Libraries that are not
// opened are not available to scripts.
//
// The program checks what it shows and exits with a value other than 0 if
// something does not behave as described, so that the CI notices.
#include <lunaris/state.h>

#include <lua.h>

#include <iostream>

namespace {
// Type of a global variable, LUA_TNIL if it does not exist.
int globalType(lua_State *luaState, const char *name) {
    const int type = lua_getglobal(luaState, name);
    lua_pop(luaState, 1);
    return type;
}
} // namespace

int main() {
    //! [open libraries]
    lunaris::State state;
    state.openLibraries(lunaris::Library::base, lunaris::Library::math);
    //! [open libraries]

    lua_State *luaState = state.luaState();
    const bool baseOpen = globalType(luaState, "print") == LUA_TFUNCTION;
    const bool mathOpen = globalType(luaState, "math") == LUA_TTABLE;
    const bool stringClosed = globalType(luaState, "string") == LUA_TNIL;

    if (!baseOpen || !mathOpen || !stringClosed) {
        std::cerr << "Unexpected libraries: print=" << baseOpen
                  << " math=" << mathOpen << " string closed=" << stringClosed
                  << '\n';
        return 1;
    }

    std::cout << "Opened base and math. string is not available.\n";
    return 0;
}
