#include "lunaris/Table.h"

#include "lunaris/State.h"

#include <lua.h>

#include <stdexcept>
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
        throw std::logic_error("operation on an invalid Lunaris Table");
    }
    return stateLifetime->state;
}

void push_value(lua_State *state, const Value &value) {
    std::visit(
        [state](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<T, std::monostate>) {
                lua_pushnil(state);
            } else if constexpr (std::same_as<T, bool>) {
                lua_pushboolean(state, item);
            } else if constexpr (std::same_as<T, std::int64_t>) {
                lua_pushinteger64(state, item);
            } else if constexpr (std::same_as<T, double>) {
                lua_pushnumber(state, item);
            } else if constexpr (std::same_as<T, std::string>) {
                lua_pushlstring(state, item.data(), item.size());
            }
        },
        value);
}

Value read_value(lua_State *state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return std::monostate{};
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0;
    case LUA_TINTEGER: {
        int isInteger = 0;
        const std::int64_t value = lua_tointeger64(state, index, &isInteger);
        if (isInteger == 0) {
            throw std::invalid_argument("Luau value is not an integer");
        }
        return value;
    }
    case LUA_TNUMBER: {
        int isNumber = 0;
        const double value = lua_tonumberx(state, index, &isNumber);
        if (isNumber == 0) {
            throw std::invalid_argument("Luau value is not a number");
        }
        return value;
    }
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char *value = lua_tolstring(state, index, &length);
        return std::string(value, length);
    }
    default:
        throw std::invalid_argument(
            "Luau reference value is not supported by lunaris::Value");
    }
}

} // namespace

Table::Table(std::weak_ptr<detail::StateLifetime> lifetime, int reference)
    : m_lifetime(std::move(lifetime)), m_reference(reference) {
}

Table::~Table() {
    release();
}

Table::Table(Table &&other) noexcept
    : m_lifetime(std::move(other.m_lifetime)),
      m_reference(std::exchange(other.m_reference, LUA_NOREF)) {
}

Table &Table::operator=(Table &&other) noexcept {
    if (this != &other) {
        release();
        m_lifetime = std::move(other.m_lifetime);
        m_reference = std::exchange(other.m_reference, LUA_NOREF);
    }
    return *this;
}

void Table::release() noexcept {
    if (m_reference != LUA_NOREF && m_reference != LUA_REFNIL) {
        if (const auto lifetime = m_lifetime.lock();
            lifetime && lifetime->state != nullptr) {
            lua_unref(lifetime->state, m_reference);
        }
    }
    m_reference = LUA_NOREF;
    m_lifetime.reset();
}

Value Table::get(std::string_view key) const {
    lua_State *state = checked_state(m_lifetime, m_reference);
    StackRestore restore(state);
    lua_getref(state, m_reference);
    const char *keyData = key.empty() ? "" : key.data();
    lua_pushlstring(state, keyData, key.size());
    lua_rawget(state, -2);
    return read_value(state, -1);
}

void Table::set(std::string_view key, const Value &value) {
    lua_State *state = checked_state(m_lifetime, m_reference);
    StackRestore restore(state);
    lua_getref(state, m_reference);
    const char *keyData = key.empty() ? "" : key.data();
    lua_pushlstring(state, keyData, key.size());
    push_value(state, value);
    lua_rawset(state, -3);
}

} // namespace lunaris
