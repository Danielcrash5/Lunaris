#include "lunaris/script_result.h"

#include "lunaris/detail/state_context.h"
#include "lunaris/error.h"

#include <lua.h>

#include <new>
#include <stdexcept>

namespace lunaris {
namespace {
// Luau's type tags depend on build options, so they are never cast to Type.
Type toType(int luaType) {
    switch (luaType) {
    case LUA_TNIL:
        return Type::nil;
    case LUA_TBOOLEAN:
        return Type::boolean;
    case LUA_TLIGHTUSERDATA:
        return Type::lightuserdata;
    case LUA_TNUMBER:
        return Type::number;
    case LUA_TVECTOR:
        return Type::vector;
    case LUA_TSTRING:
        return Type::string;
    case LUA_TTABLE:
        return Type::table;
    case LUA_TFUNCTION:
        return Type::function;
    case LUA_TUSERDATA:
        return Type::userdata;
    case LUA_TTHREAD:
        return Type::thread;
    case LUA_TBUFFER:
        return Type::buffer;
    default:
        throw std::logic_error("lunaris: unsupported Luau type");
    }
}
} // namespace

ScriptResult::~ScriptResult() {
    release();
}

ScriptResult &ScriptResult::operator=(ScriptResult &&other) noexcept {
    if (this != &other) {
        release();
        m_stateContext = std::move(other.m_stateContext);
        m_references = std::move(other.m_references);
        other.m_references.clear();
    }
    return *this;
}

std::uint32_t ScriptResult::size() const noexcept {
    return static_cast<std::uint32_t>(m_references.size());
}

Type ScriptResult::type(std::uint32_t index) const {
    if (index >= m_references.size()) {
        throw std::out_of_range("lunaris: ScriptResult index out of range");
    }
    lua_State *luaState = m_stateContext ? m_stateContext->luaState() : nullptr;
    if (luaState == nullptr) {
        throw Error(ErrorDetails{
            .kind = ErrorKind::state,
            .message = "lunaris: the State of this ScriptResult is closed"});
    }
    if (lua_checkstack(luaState, 1) == 0) {
        throw std::bad_alloc();
    }
    lua_getref(luaState, m_references[index]);
    const int luaType = lua_type(luaState, -1);
    lua_pop(luaState, 1);
    return toType(luaType);
}

void ScriptResult::release() noexcept {
    if (m_stateContext) {
        lua_State *luaState = m_stateContext->luaState();
        if (luaState != nullptr) {
            for (const int reference : m_references) {
                lua_unref(luaState, reference);
            }
        }
    }
    m_references.clear();
    m_stateContext.reset();
}

} // namespace lunaris
