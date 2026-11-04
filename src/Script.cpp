#include "lunaris/Script.h"

#include "lunaris/State.h"

#include <lua.h>

#include <string>
#include <utility>

namespace lunaris {
namespace {

class StackRestore {
  public:
    explicit StackRestore(lua_State *state)
        : m_state(state), m_top(lua_gettop(state)) {
    }

    ~StackRestore() {
        lua_settop(m_state, m_top);
    }

  private:
    lua_State *m_state;
    int m_top;
};

lua_State *checked_state(const std::weak_ptr<detail::StateLifetime> &lifetime,
                         int reference) {
    const auto stateLifetime = lifetime.lock();
    if (!stateLifetime || stateLifetime->state == nullptr ||
        reference == LUA_NOREF || reference == LUA_REFNIL) {
        throw std::logic_error("operation on an invalid Lunaris Script");
    }
    return stateLifetime->state;
}

std::string error_message(lua_State *state) {
    std::size_t length = 0;
    const char *message = lua_tolstring(state, -1, &length);
    if (message == nullptr) {
        return "Luau execution failed with a non-string error value";
    }
    return std::string(message, length);
}

} // namespace

Script::Script(std::weak_ptr<detail::StateLifetime> lifetime, int reference,
               std::string source)
    : m_lifetime(std::move(lifetime)), m_reference(reference),
      m_source(std::move(source)) {
}

Script::~Script() {
    release();
}

Script::Script(Script &&other) noexcept
    : m_lifetime(std::move(other.m_lifetime)),
      m_reference(std::exchange(other.m_reference, LUA_NOREF)),
      m_source(std::move(other.m_source)) {
}

Script &Script::operator=(Script &&other) noexcept {
    if (this != &other) {
        release();
        m_lifetime = std::move(other.m_lifetime);
        m_reference = std::exchange(other.m_reference, LUA_NOREF);
        m_source = std::move(other.m_source);
    }
    return *this;
}

void Script::release() noexcept {
    if (m_reference != LUA_NOREF && m_reference != LUA_REFNIL) {
        if (const auto lifetime = m_lifetime.lock();
            lifetime && lifetime->state != nullptr) {
            lua_unref(lifetime->state, m_reference);
        }
    }
    m_reference = LUA_NOREF;
    m_lifetime.reset();
    m_source.clear();
}

Result<std::vector<Value>> Script::execute() {
    lua_State *state = checked_state(m_lifetime, m_reference);
    StackRestore restore(state);
    const int initialTop = lua_gettop(state);

    lua_getref(state, m_reference);
    const int status = lua_pcall(state, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const ErrorType type =
            status == LUA_ERRMEM ? ErrorType::Memory : ErrorType::Runtime;
        return Result<std::vector<Value>>::failure(
            Error(type, error_message(state), m_source));
    }

    const int resultCount = lua_gettop(state) - initialTop;
    std::vector<Value> values;
    values.reserve(static_cast<std::size_t>(resultCount));
    for (int index = initialTop + 1; index <= initialTop + resultCount;
         ++index) {
        const int type = lua_type(state, index);
        if (type != LUA_TNIL && type != LUA_TBOOLEAN && type != LUA_TINTEGER &&
            type != LUA_TNUMBER && type != LUA_TSTRING) {
            return Result<std::vector<Value>>::failure(Error(
                ErrorType::Type,
                "script returned a reference value not supported by Value",
                m_source));
        }

        switch (type) {
        case LUA_TNIL:
            values.emplace_back(std::monostate{});
            break;
        case LUA_TBOOLEAN:
            values.emplace_back(lua_toboolean(state, index) != 0);
            break;
        case LUA_TINTEGER: {
            int isInteger = 0;
            const std::int64_t value =
                lua_tointeger64(state, index, &isInteger);
            if (isInteger == 0) {
                return Result<std::vector<Value>>::failure(
                    Error(ErrorType::Type, "script returned an invalid integer",
                          m_source));
            }
            values.emplace_back(value);
            break;
        }
        case LUA_TNUMBER: {
            int isNumber = 0;
            const double value = lua_tonumberx(state, index, &isNumber);
            if (isNumber == 0) {
                return Result<std::vector<Value>>::failure(
                    Error(ErrorType::Type, "script returned an invalid number",
                          m_source));
            }
            values.emplace_back(value);
            break;
        }
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char *value = lua_tolstring(state, index, &length);
            values.emplace_back(std::string(value, length));
            break;
        }
        default:
            break;
        }
    }

    return Result<std::vector<Value>>::success(std::move(values));
}

} // namespace lunaris
