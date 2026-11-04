#include "lunaris/State.h"

#include <lua.h>

#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace lunaris {

struct State::AllocatorContext {
    Allocator::AllocateFn allocate = nullptr;
    void *userData = nullptr;
    std::optional<std::size_t> memoryLimit;
    std::size_t allocatedBytes = 0;
};

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

void *default_allocate(void *, void *pointer, std::size_t,
                       std::size_t newSize) {
    if (newSize == 0) {
        std::free(pointer);
        return nullptr;
    }

    return std::realloc(pointer, newSize);
}

} // namespace

void *State::tracked_allocate(void *userData, void *pointer,
                              std::size_t oldSize, std::size_t newSize) {
    auto &context = *static_cast<AllocatorContext *>(userData);
    const std::size_t previousSize = pointer != nullptr ? oldSize : 0;

    if (newSize == 0) {
        void *result =
            context.allocate(context.userData, pointer, oldSize, newSize);
        context.allocatedBytes -= previousSize;
        return result;
    }

    if (previousSize > context.allocatedBytes) {
        return nullptr;
    }

    const std::size_t retainedBytes = context.allocatedBytes - previousSize;
    if (newSize > std::numeric_limits<std::size_t>::max() - retainedBytes) {
        return nullptr;
    }

    if (context.memoryLimit.has_value() &&
        (retainedBytes > *context.memoryLimit ||
         newSize > *context.memoryLimit - retainedBytes)) {
        return nullptr;
    }

    void *result =
        context.allocate(context.userData, pointer, oldSize, newSize);
    if (result != nullptr) {
        context.allocatedBytes = retainedBytes + newSize;
    }

    return result;
}

State::State(const StateConfig &config) {
    m_allocatorContext = std::make_unique<AllocatorContext>();
    m_lifetime = std::make_shared<detail::StateLifetime>();
    m_allocatorContext->allocate = config.allocator.allocate != nullptr
                                       ? config.allocator.allocate
                                       : &default_allocate;
    m_allocatorContext->userData = config.allocator.userData;
    m_allocatorContext->memoryLimit = config.memoryLimit;

    m_state = lua_newstate(&State::tracked_allocate, m_allocatorContext.get());
    if (m_state == nullptr) {
        throw std::bad_alloc{};
    }
    m_lifetime->state = m_state;
}

State::~State() {
    if (m_state != nullptr) {
        m_lifetime->state = nullptr;
        lua_close(m_state);
    }
}

State::State(State &&other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)),
      m_allocatorContext(std::move(other.m_allocatorContext)),
      m_lifetime(std::move(other.m_lifetime)) {
}

State &State::operator=(State &&other) noexcept {
    if (this != &other) {
        if (m_state != nullptr) {
            m_lifetime->state = nullptr;
            lua_close(m_state);
        }

        m_allocatorContext = std::move(other.m_allocatorContext);
        m_lifetime = std::move(other.m_lifetime);
        m_state = std::exchange(other.m_state, nullptr);
    }

    return *this;
}

lua_State *State::native_handle() noexcept {
    return m_state;
}

lua_State *State::checked_state() const {
    if (m_state == nullptr) {
        throw std::logic_error("operation on a moved-from Lunaris State");
    }
    return m_state;
}

int State::top() const {
    return lua_gettop(checked_state());
}

void State::pop(int count) {
    if (count < 0) {
        throw std::invalid_argument("stack pop count cannot be negative");
    }

    lua_State *state = checked_state();
    if (count > lua_gettop(state)) {
        throw std::out_of_range("stack pop count exceeds stack height");
    }
    lua_settop(state, -count - 1);
}

void State::clear() {
    lua_settop(checked_state(), 0);
}

void State::push(std::nullptr_t) {
    lua_pushnil(checked_state());
}

void State::push(bool value) {
    lua_pushboolean(checked_state(), value);
}

void State::push(std::int64_t value) {
    lua_pushinteger64(checked_state(), value);
}

void State::push(double value) {
    lua_pushnumber(checked_state(), value);
}

void State::push(std::string_view value) {
    const char *data = value.empty() ? "" : value.data();
    lua_pushlstring(checked_state(), data, value.size());
}

void State::push(const Value &value) {
    std::visit(
        [this](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<T, std::monostate>) {
                push(nullptr);
            } else {
                push(item);
            }
        },
        value);
}

Table State::create_table() {
    lua_State *state = checked_state();
    lua_createtable(state, 0, 0);
    const int reference = lua_ref(state, -1);
    lua_pop(state, 1);
    return Table(m_lifetime, reference);
}

Table State::get_table(int index) {
    validate_index(index);
    lua_State *state = checked_state();
    if (lua_type(state, index) != LUA_TTABLE) {
        throw std::invalid_argument("Luau value is not a table");
    }

    const int reference = lua_ref(state, index);
    return Table(m_lifetime, reference);
}

Result<Value> State::get_global(std::string_view name) const {
    if (name.find('\0') != std::string_view::npos) {
        return Result<Value>::failure(Error(
            ErrorType::Type, "global name contains an embedded null byte"));
    }

    lua_State *state = checked_state();
    StackRestore restore(state);
    const std::string globalName(name);
    lua_getglobal(state, globalName.c_str());

    const int type = lua_type(state, -1);
    if (type != LUA_TNIL && type != LUA_TBOOLEAN && type != LUA_TINTEGER &&
        type != LUA_TNUMBER && type != LUA_TSTRING) {
        return Result<Value>::failure(
            Error(ErrorType::Type, "global value is not a scalar"));
    }

    return Result<Value>::success(get_value(-1));
}

Result<void> State::set_global(std::string_view name, const Value &value) {
    if (name.find('\0') != std::string_view::npos) {
        return Result<void>::failure(Error(
            ErrorType::Type, "global name contains an embedded null byte"));
    }

    lua_State *state = checked_state();
    const std::string globalName(name);
    push(value);
    lua_setglobal(state, globalName.c_str());
    return Result<void>::success();
}

void State::validate_index(int index) const {
    const int stackTop = lua_gettop(checked_state());
    if (index == 0 || index > stackTop || index < -stackTop) {
        throw std::out_of_range("Luau stack index is out of range");
    }
}

bool State::get_boolean(int index) const {
    lua_State *state = checked_state();
    if (lua_type(state, index) != LUA_TBOOLEAN) {
        throw std::invalid_argument("Luau value is not a boolean");
    }
    return lua_toboolean(state, index) != 0;
}

std::int64_t State::get_integer(int index) const {
    lua_State *state = checked_state();
    if (lua_type(state, index) != LUA_TINTEGER) {
        throw std::invalid_argument("Luau value is not an integer");
    }

    int isInteger = 0;
    const std::int64_t value = lua_tointeger64(state, index, &isInteger);
    if (isInteger == 0) {
        throw std::invalid_argument("Luau value is not an integer");
    }
    return value;
}

double State::get_number(int index) const {
    lua_State *state = checked_state();
    const int type = lua_type(state, index);
    if (type == LUA_TINTEGER) {
        return static_cast<double>(get_integer(index));
    }
    if (type != LUA_TNUMBER) {
        throw std::invalid_argument("Luau value is not a number");
    }

    int isNumber = 0;
    const double value = lua_tonumberx(state, index, &isNumber);
    if (isNumber == 0) {
        throw std::invalid_argument("Luau value is not a number");
    }
    return value;
}

std::string State::get_string(int index) const {
    lua_State *state = checked_state();
    if (lua_type(state, index) != LUA_TSTRING) {
        throw std::invalid_argument("Luau value is not a string");
    }

    std::size_t length = 0;
    const char *value = lua_tolstring(state, index, &length);
    return std::string(value, length);
}

Value State::get_value(int index) const {
    lua_State *state = checked_state();
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return std::monostate{};
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0;
    case LUA_TNUMBER:
        return get_number(index);
    case LUA_TINTEGER:
        return get_integer(index);
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

} // namespace lunaris
