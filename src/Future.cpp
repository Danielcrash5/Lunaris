#include "lunaris/State.h"

#include "lunaris/Binding.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <atomic>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace lunaris {
namespace {

constexpr char kFutureTypeMarker = 0;
constexpr const char *kFutureMetatable = "lunaris.Future.metatable.v1";

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

struct FutureUserdata {
    std::shared_ptr<Future<Value>> future;
    std::weak_ptr<detail::StateLifetime> lifetime;
    bool awaitRegistered = false;
};

class ScopedReference {
  public:
    ScopedReference(lua_State *state, int reference)
        : m_state(state), m_reference(reference) {
    }

    ~ScopedReference() {
        lua_unref(m_state, m_reference);
    }

    ScopedReference(const ScopedReference &) = delete;
    ScopedReference &operator=(const ScopedReference &) = delete;

  private:
    lua_State *m_state;
    int m_reference;
};

void destroy_future_userdata(lua_State *, void *data) {
    static_cast<FutureUserdata *>(data)->~FutureUserdata();
}

FutureUserdata *get_future_userdata(lua_State *state, int index) {
    if (lua_type(state, index) != LUA_TUSERDATA ||
        !lua_getmetatable(state, index)) {
        throw std::invalid_argument("expected a Lunaris Future");
    }
    lua_getfield(state, -1, "__lunaris_future");
    const bool matches = lua_touserdata(state, -1) == &kFutureTypeMarker;
    lua_pop(state, 2);
    if (!matches) {
        throw std::invalid_argument("expected a Lunaris Future");
    }
    auto *userdata =
        static_cast<FutureUserdata *>(lua_touserdata(state, index));
    if (userdata == nullptr || userdata->future == nullptr ||
        !userdata->future->valid()) {
        throw std::invalid_argument("Future handle is not initialized");
    }
    return userdata;
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
            } else {
                lua_pushlstring(state, item.data(), item.size());
            }
        },
        value);
}

std::string read_error(lua_State *state) {
    std::size_t length = 0;
    const char *message = lua_tolstring(state, -1, &length);
    if (message == nullptr) {
        return "Luau coroutine failed with a non-string error value";
    }
    return std::string(message, length);
}

Result<void>
resume_awaiting_coroutine(lua_State *mainState, int threadRef,
                          const std::shared_ptr<Future<Value>> &future) {
    ScopedReference releaseThread(mainState, threadRef);
    StackRestore restore(mainState);
    lua_getref(mainState, threadRef);
    lua_State *coroutine = lua_tothread(mainState, -1);
    lua_pop(mainState, 1);
    if (coroutine == nullptr) {
        return Result<void>::failure(
            Error(ErrorType::Runtime, "awaiting coroutine is no longer valid"));
    }

    auto result = future->get();
    if (result) {
        lua_pushboolean(coroutine, true);
        push_value(coroutine, result.value());
    } else {
        lua_pushboolean(coroutine, false);
        const auto message = result.error().message();
        lua_pushlstring(coroutine, message.data(), message.size());
    }
    const int status = lua_resume(coroutine, mainState, 2);
    if (status != LUA_OK && status != LUA_YIELD) {
        return Result<void>::failure(
            Error(status == LUA_ERRMEM ? ErrorType::Memory : ErrorType::Runtime,
                  read_error(coroutine)));
    }
    return Result<void>::success();
}

int future_await_continuation(lua_State *state, int) {
    if (lua_gettop(state) < 2 || lua_type(state, -2) != LUA_TBOOLEAN) {
        lua_pushstring(state, "Future await resumed with an invalid result");
        lua_error(state);
        return 0;
    }
    if (!lua_toboolean(state, -2)) {
        lua_pushvalue(state, -1);
        lua_error(state);
        return 0;
    }
    return 1;
}

int future_await(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        if (lua_gettop(state) != 1) {
            throw std::invalid_argument("Future:await expects no arguments");
        }
        auto *userdata = get_future_userdata(state, 1);
        auto future = userdata->future;
        if (future->is_ready()) {
            auto result = future->get();
            if (!result) {
                throw std::runtime_error(std::string(result.error().message()));
            }
            push_value(state, result.value());
            return 1;
        }
        if (!lua_isyieldable(state)) {
            throw std::logic_error(
                "Future:await must run in a yieldable coroutine");
        }

        const auto lifetime = userdata->lifetime.lock();
        if (!lifetime || lifetime->state == nullptr ||
            lua_mainthread(state) != lifetime->state) {
            throw std::logic_error(
                "Future belongs to a different or closed State");
        }
        if (userdata->awaitRegistered) {
            throw std::logic_error("Future:await can only be registered once");
        }

        auto request = std::make_shared<detail::PendingResume>();
        request->ready = std::make_shared<std::atomic_bool>(false);
        lua_pushthread(state);
        const int threadReference = lua_ref(state, -1);
        lua_pop(state, 1);
        request->resume = [threadReference,
                           future](lua_State *mainState) -> Result<void> {
            try {
                return resume_awaiting_coroutine(mainState, threadReference,
                                                 future);
            } catch (const std::exception &exception) {
                return Result<void>::failure(
                    Error(ErrorType::Runtime, exception.what()));
            } catch (...) {
                return Result<void>::failure(
                    Error(ErrorType::Runtime,
                          "Future await resume failed with a non-standard "
                          "exception"));
            }
        };
        try {
            {
                std::lock_guard lock(lifetime->pendingResumesMutex);
                lifetime->pendingResumes.push_back(request);
            }
            future->observe_ready(request->ready);
            userdata->awaitRegistered = true;
        } catch (...) {
            {
                std::lock_guard lock(lifetime->pendingResumesMutex);
                lifetime->pendingResumes.remove(request);
            }
            lua_unref(state, threadReference);
            throw;
        }
        return lua_yield(state, 0);
    });
}

int future_index(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        (void)get_future_userdata(state, 1);
        if (lua_type(state, 2) != LUA_TSTRING) {
            lua_pushnil(state);
            return 1;
        }
        std::size_t length = 0;
        const char *key = lua_tolstring(state, 2, &length);
        if (std::string_view(key, length) == "await") {
            lua_pushcclosurek(state, future_await, "Future.await", 0,
                              future_await_continuation);
        } else {
            lua_pushnil(state);
        }
        return 1;
    });
}

void install_future_metatable(lua_State *state) {
    if (luaL_newmetatable(state, kFutureMetatable) == 0) {
        lua_getfield(state, -1, "__lunaris_future");
        const bool matches = lua_touserdata(state, -1) == &kFutureTypeMarker;
        lua_pop(state, 1);
        lua_pop(state, 1);
        if (!matches) {
            throw std::runtime_error("Future metatable name is already in use");
        }
        return;
    }
    const int metatable = lua_gettop(state);
    lua_pushlightuserdata(state, const_cast<char *>(&kFutureTypeMarker));
    lua_setfield(state, metatable, "__lunaris_future");
    lua_pushcclosure(state, future_index, "Future.__index", 0);
    lua_setfield(state, metatable, "__index");
    lua_pushstring(state, "protected Future");
    lua_setfield(state, metatable, "__metatable");
    lua_setreadonly(state, metatable, true);
    lua_pop(state, 1);
}

} // namespace

void State::set_future_global(std::string_view name, Future<Value> future) {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "Future global name must be non-empty and contain no null byte");
    }
    if (!future.valid()) {
        throw std::invalid_argument("cannot install an invalid Future");
    }

    lua_State *lua = checked_state();
    StackRestore restore(lua);
    const std::string globalName(name);
    lua_getglobal(lua, globalName.c_str());
    const bool exists = !lua_isnil(lua, -1);
    lua_pop(lua, 1);
    if (exists) {
        throw std::invalid_argument("global '" + globalName +
                                    "' is already registered");
    }

    install_future_metatable(lua);
    auto *userdata = static_cast<FutureUserdata *>(lua_newuserdatadtor(
        lua, sizeof(FutureUserdata), destroy_future_userdata));
    if (userdata == nullptr) {
        throw std::bad_alloc{};
    }
    new (userdata) FutureUserdata{
        std::make_shared<Future<Value>>(std::move(future)), m_lifetime};
    lua_getfield(lua, LUA_REGISTRYINDEX, kFutureMetatable);
    if (lua_type(lua, -1) != LUA_TTABLE) {
        throw std::runtime_error("Future metatable is not installed");
    }
    lua_setmetatable(lua, -2);
    lua_setglobal(lua, globalName.c_str());
}

void State::open_coroutine_library() {
    lua_State *lua = checked_state();
    StackRestore restore(lua);
    lua_getglobal(lua, "coroutine");
    const bool exists = !lua_isnil(lua, -1);
    lua_pop(lua, 1);
    if (exists) {
        throw std::invalid_argument("global 'coroutine' is already registered");
    }
    luaopen_coroutine(lua);
    lua_setglobal(lua, "coroutine");
}

Result<std::size_t> State::resume_ready() {
    lua_State *lua = checked_state();
    std::size_t resumed = 0;
    while (true) {
        std::shared_ptr<detail::PendingResume> request;
        {
            std::lock_guard lock(m_lifetime->pendingResumesMutex);
            const auto iterator = std::find_if(
                m_lifetime->pendingResumes.begin(),
                m_lifetime->pendingResumes.end(), [](const auto &candidate) {
                    return candidate->ready->load(std::memory_order_acquire);
                });
            if (iterator == m_lifetime->pendingResumes.end()) {
                break;
            }
            request = std::move(*iterator);
            m_lifetime->pendingResumes.erase(iterator);
        }

        auto result = request->resume(lua);
        if (!result) {
            return Result<std::size_t>::failure(result.error());
        }
        ++resumed;
    }
    return Result<std::size_t>::success(resumed);
}

} // namespace lunaris
