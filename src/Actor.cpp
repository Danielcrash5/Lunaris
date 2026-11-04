#include "lunaris/Actor.h"

#include "lunaris/Binding.h"
#include "lunaris/Error.h"
#include "lunaris/State.h"

#include <lua.h>
#include <lualib.h>

#include <cstdint>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace lunaris {
namespace {

constexpr char kActorTypeMarker = 0;
constexpr const char *kActorMetatable = "lunaris.Actor.metatable.v1";

class StackRestore {
  public:
    explicit StackRestore(lua_State *state)
        : m_state(state), m_top(lua_gettop(state)) {
    }

    ~StackRestore() {
        lua_settop(m_state, m_top);
    }

    StackRestore(const StackRestore &) = delete;
    StackRestore &operator=(const StackRestore &) = delete;

  private:
    lua_State *m_state;
    int m_top;
};

struct ActorUserdata {
    std::shared_ptr<Actor> actor;
    std::weak_ptr<detail::StateLifetime> lifetime;
};

struct ActorLuaCallback {
    std::weak_ptr<detail::StateLifetime> lifetime;
    int reference = LUA_NOREF;
};

void destroy_actor_userdata(lua_State *, void *data) {
    static_cast<ActorUserdata *>(data)->~ActorUserdata();
}

ActorUserdata *get_actor_userdata(lua_State *state, int index) {
    if (lua_type(state, index) != LUA_TUSERDATA ||
        !lua_getmetatable(state, index)) {
        throw std::invalid_argument("expected an Actor handle");
    }
    lua_getfield(state, -1, "__lunaris_actor");
    const bool matches = lua_touserdata(state, -1) == &kActorTypeMarker;
    lua_pop(state, 2);
    if (!matches) {
        throw std::invalid_argument("expected an Actor handle");
    }
    auto *userdata = static_cast<ActorUserdata *>(lua_touserdata(state, index));
    if (userdata == nullptr || userdata->actor == nullptr) {
        throw std::invalid_argument("Actor handle is not initialized");
    }
    return userdata;
}

Value read_message_value(lua_State *state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return std::monostate{};
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0;
    case LUA_TINTEGER: {
        int isInteger = 0;
        const auto value = lua_tointeger64(state, index, &isInteger);
        if (isInteger == 0) {
            throw std::invalid_argument("message integer is out of range");
        }
        return static_cast<std::int64_t>(value);
    }
    case LUA_TNUMBER: {
        int isNumber = 0;
        const double value = lua_tonumberx(state, index, &isNumber);
        if (isNumber == 0) {
            throw std::invalid_argument("message value must be a number");
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
            "actor messages support only nil, booleans, numbers, and strings");
    }
}

void push_message_value(lua_State *state, const Value &value) {
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
            } else {
                lua_pushlstring(state, item.data(), item.size());
            }
        },
        value);
}

std::string read_luau_error(lua_State *state) {
    std::size_t length = 0;
    const char *message = lua_tolstring(state, -1, &length);
    if (message == nullptr) {
        return "Luau actor handler failed with a non-string error value";
    }
    return std::string(message, length);
}

int actor_bind_message(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        if (lua_gettop(state) != 3) {
            throw std::invalid_argument(
                "Actor:BindToMessage expects a topic and function");
        }
        auto *userdata = get_actor_userdata(state, 1);
        if (lua_type(state, 2) != LUA_TSTRING) {
            throw std::invalid_argument("actor message topic must be a string");
        }
        if (lua_type(state, 3) != LUA_TFUNCTION) {
            throw std::invalid_argument(
                "actor message handler must be a function");
        }
        const auto lifetime = userdata->lifetime.lock();
        if (!lifetime || lifetime->state != state) {
            throw std::logic_error(
                "Actor handle is not bound to this Luau State");
        }

        std::size_t length = 0;
        const char *topic = lua_tolstring(state, 2, &length);
        auto callback = std::make_shared<ActorLuaCallback>();
        callback->lifetime = lifetime;
        callback->reference = lua_ref(state, 3);

        const std::string ownedTopic(topic, length);
        try {
            userdata->actor->bind(
                ownedTopic, [callback](const Message &message) -> Result<void> {
                    const auto callbackLifetime = callback->lifetime.lock();
                    if (!callbackLifetime ||
                        callbackLifetime->state == nullptr) {
                        return Result<void>::failure(Error(
                            ErrorType::Runtime,
                            "Actor handler State is no longer available"));
                    }
                    lua_State *callbackState = callbackLifetime->state;
                    StackRestore restore(callbackState);
                    lua_getref(callbackState, callback->reference);
                    if (lua_type(callbackState, -1) != LUA_TFUNCTION) {
                        return Result<void>::failure(Error(
                            ErrorType::Runtime,
                            "Actor handler reference is no longer valid"));
                    }
                    push_message_value(callbackState, message.payload);
                    const int status = lua_pcall(callbackState, 1, 0, 0);
                    if (status != LUA_OK) {
                        const ErrorType type = status == LUA_ERRMEM
                                                   ? ErrorType::Memory
                                                   : ErrorType::Runtime;
                        return Result<void>::failure(
                            Error(type, read_luau_error(callbackState)));
                    }
                    return Result<void>::success();
                });
        } catch (...) {
            lua_unref(state, callback->reference);
            throw;
        }
        return 0;
    });
}

int actor_send_message(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        if (lua_gettop(state) != 3) {
            throw std::invalid_argument(
                "Actor:SendMessage expects a topic and scalar payload");
        }
        auto *userdata = get_actor_userdata(state, 1);
        if (lua_type(state, 2) != LUA_TSTRING) {
            throw std::invalid_argument("actor message topic must be a string");
        }
        std::size_t length = 0;
        const char *topic = lua_tolstring(state, 2, &length);
        const Value payload = read_message_value(state, 3);
        lua_pushboolean(state, userdata->actor->send(
                                   std::string_view(topic, length), payload));
        return 1;
    });
}

int actor_index(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        (void)get_actor_userdata(state, 1);
        if (lua_type(state, 2) != LUA_TSTRING) {
            lua_pushnil(state);
            return 1;
        }
        std::size_t length = 0;
        const char *key = lua_tolstring(state, 2, &length);
        const std::string_view method(key, length);
        if (method == "SendMessage") {
            lua_pushcclosure(state, actor_send_message, "Actor.SendMessage", 0);
        } else if (method == "BindToMessage") {
            lua_pushcclosure(state, actor_bind_message, "Actor.BindToMessage",
                             0);
        } else {
            lua_pushnil(state);
        }
        return 1;
    });
}

void install_actor_metatable(lua_State *state) {
    if (luaL_newmetatable(state, kActorMetatable) == 0) {
        lua_getfield(state, -1, "__lunaris_actor");
        const bool matches = lua_touserdata(state, -1) == &kActorTypeMarker;
        lua_pop(state, 1);
        lua_pop(state, 1);
        if (!matches) {
            throw std::runtime_error("Actor metatable name is already in use");
        }
        return;
    }
    const int metatable = lua_gettop(state);
    lua_pushlightuserdata(state, const_cast<char *>(&kActorTypeMarker));
    lua_setfield(state, metatable, "__lunaris_actor");
    lua_pushcclosure(state, actor_index, "Actor.__index", 0);
    lua_setfield(state, metatable, "__index");
    lua_pushstring(state, "protected Actor");
    lua_setfield(state, metatable, "__metatable");
    lua_setreadonly(state, metatable, true);
    lua_pop(state, 1);
}

} // namespace

void Actor::validate_topic(std::string_view topic) const {
    if (topic.empty() || topic.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "actor topic must be non-empty and contain no null byte");
    }
}

void Actor::bind(std::string_view topic, Handler handler) {
    validate_topic(topic);
    if (!handler) {
        throw std::invalid_argument("actor handler cannot be empty");
    }
    std::lock_guard lock(m_handlersMutex);
    const auto [iterator, inserted] =
        m_handlers.emplace(std::string(topic), std::move(handler));
    if (!inserted) {
        throw std::invalid_argument("actor topic already has a handler");
    }
}

bool Actor::unbind(std::string_view topic) {
    validate_topic(topic);
    std::lock_guard lock(m_handlersMutex);
    return m_handlers.erase(std::string(topic)) != 0;
}

bool Actor::send(std::string_view topic, Value payload) {
    return m_messages.send(topic, std::move(payload));
}

Result<bool> Actor::dispatch_one_locked() {
    auto message = m_messages.try_receive();
    if (!message) {
        return Result<bool>::success(false);
    }

    Handler handler;
    {
        std::lock_guard lock(m_handlersMutex);
        const auto iterator = m_handlers.find(message->topic);
        if (iterator == m_handlers.end()) {
            return Result<bool>::failure(
                Error(ErrorType::Runtime, "actor has no handler for topic '" +
                                              message->topic + "'"));
        }
        handler = iterator->second;
    }

    try {
        auto result = handler(*message);
        if (!result) {
            return Result<bool>::failure(result.error());
        }
    } catch (const std::exception &exception) {
        return Result<bool>::failure(
            Error(ErrorType::Runtime, "actor handler threw an exception: " +
                                          std::string(exception.what())));
    } catch (...) {
        return Result<bool>::failure(
            Error(ErrorType::Runtime,
                  "actor handler threw a non-standard exception"));
    }
    return Result<bool>::success(true);
}

Result<bool> Actor::dispatch_one() {
    std::unique_lock lock(m_dispatchMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return Result<bool>::failure(
            Error(ErrorType::Runtime, "actor dispatch is already in progress"));
    }
    return dispatch_one_locked();
}

Result<std::size_t> Actor::dispatch_pending() {
    std::unique_lock lock(m_dispatchMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return Result<std::size_t>::failure(
            Error(ErrorType::Runtime, "actor dispatch is already in progress"));
    }
    std::size_t dispatched = 0;
    while (true) {
        auto result = dispatch_one_locked();
        if (!result) {
            return Result<std::size_t>::failure(result.error());
        }
        if (!result.value()) {
            return Result<std::size_t>::success(dispatched);
        }
        ++dispatched;
    }
}

void Actor::close() {
    m_messages.close();
}

bool Actor::is_closed() const {
    return m_messages.is_closed();
}

std::size_t Actor::pending() const {
    return m_messages.size();
}

void Actor::install(State &state, std::string_view globalName,
                    std::shared_ptr<Actor> actor) {
    if (globalName.empty() || globalName.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "Actor global name must be non-empty and contain no null byte");
    }
    if (actor == nullptr) {
        throw std::invalid_argument("Actor handle cannot be null");
    }

    lua_State *lua = state.checked_state();
    StackRestore restore(lua);
    const std::string name(globalName);
    lua_getglobal(lua, name.c_str());
    const bool exists = !lua_isnil(lua, -1);
    lua_pop(lua, 1);
    if (exists) {
        throw std::invalid_argument("global '" + name +
                                    "' is already registered");
    }

    install_actor_metatable(lua);
    auto *userdata = static_cast<ActorUserdata *>(lua_newuserdatadtor(
        lua, sizeof(ActorUserdata), destroy_actor_userdata));
    if (userdata == nullptr) {
        throw std::bad_alloc{};
    }
    new (userdata) ActorUserdata{std::move(actor), state.m_lifetime};
    lua_getfield(lua, LUA_REGISTRYINDEX, kActorMetatable);
    if (lua_type(lua, -1) != LUA_TTABLE) {
        throw std::runtime_error("Actor metatable is not installed");
    }
    lua_setmetatable(lua, -2);
    lua_setglobal(lua, name.c_str());
}

} // namespace lunaris
