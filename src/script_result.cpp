#include "lunaris/script_result.h"

#include "lunaris/detail/state_context.h"
#include "lunaris/detail/stack_guard.h"
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

lua_State *requireOpenState(
    const std::shared_ptr<detail::StateContext> &stateContext) {
    lua_State *state = stateContext ? stateContext->luaState() : nullptr;
    if (state == nullptr) {
        throw Error(ErrorDetails{
            .kind = ErrorKind::state,
            .message = "lunaris: the State of this ScriptResult is closed"});
    }
    return state;
}

template <typename T, typename Converter>
T getReferencedValue(lua_State *state, int reference, int expectedLuaType,
                     const char *typeErrorMessage, Converter converter) {
    if (lua_checkstack(state, 1) == 0) {
        throw std::bad_alloc();
    }
    [[maybe_unused]] const detail::StackGuard stackGuard(state);

    // A registry reference is an ID, not a stack index. Push its value first.
    lua_getref(state, reference);
    if (lua_type(state, -1) != expectedLuaType) {
        throw Error(ErrorDetails{
            .kind = ErrorKind::type,
            .message = typeErrorMessage});
    }
    return converter(state, -1);
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

bool ScriptResult::getBool(std::uint32_t index) const {
    if (index >= size()) {
        throw std::out_of_range("lunaris: ScriptResult index out of range");
    }
    lua_State *state = requireOpenState(m_stateContext);
    return getReferencedValue<bool>(
        state, m_references[index], LUA_TBOOLEAN, "lunaris: value is not a boolean",
        [](lua_State *luaState, int stackIndex) {
            return lua_toboolean(luaState, stackIndex) != 0;
        });
}

double ScriptResult::getDouble(std::uint32_t index) const {
    if (index >= size()) {
        throw std::out_of_range("lunaris: ScriptResult index out of range");
    }
    lua_State *state = requireOpenState(m_stateContext);
    return getReferencedValue<double>(
        state, m_references[index], LUA_TNUMBER, "lunaris: value is not a number",
        [](lua_State *luaState, int stackIndex) {
            return lua_tonumber(luaState, stackIndex);
        });
}

std::string ScriptResult::getString(std::uint32_t index) const {
    if (index >= size()) {
        throw std::out_of_range("lunaris: ScriptResult index out of range");
    }
    lua_State *state = requireOpenState(m_stateContext);
    return getReferencedValue<std::string>(
        state, m_references[index], LUA_TSTRING, "lunaris: value is not a string",
        [](lua_State *luaState, int stackIndex) {
            std::size_t length = 0;
            const char *value = lua_tolstring(luaState, stackIndex, &length);
            return value == nullptr ? std::string{} : std::string(value, length);
        });
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
