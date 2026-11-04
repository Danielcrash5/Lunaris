#pragma once

#include "lunaris/detail/StateLifetime.h"

#include <lua.h>
#include <lualib.h>

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace lunaris {

/// Describes one C++ constructor signature for a usertype.
template <typename T, typename... Args> struct ConstructorDescriptor {};

/// Creates a descriptor for the single constructor exposed as Type.new(...).
template <typename T, typename... Args>
[[nodiscard]] constexpr ConstructorDescriptor<T, Args...> constructor() {
    return {};
}

/// Describes a public data-member property on a usertype.
template <typename Class, typename Member> struct PropertyDescriptor {
    std::string name;
    Member Class::*member;
};

/// Creates a read/write property, or a read-only property for const members.
template <typename Class, typename Member>
    requires(!std::is_function_v<Member>)
[[nodiscard]] PropertyDescriptor<Class, Member>
property(std::string_view name, Member Class::*member) {
    return {std::string(name), member};
}

/// Describes a read-only property backed by a getter.
template <typename Class, typename Getter> struct GetterPropertyDescriptor {
    std::string name;
    Getter getter;
};

/// Describes a property backed by a getter and setter.
template <typename Class, typename Getter, typename Setter>
struct GetterSetterPropertyDescriptor {
    std::string name;
    Getter getter;
    Setter setter;
};

/// Describes a non-overloaded C++ member function on a usertype.
template <typename Class, typename Method> struct MethodDescriptor {
    std::string name;
    Method method;
};

namespace detail {

template <typename Method> struct MemberFunctionClass;
template <typename Return, typename Class, typename... Args>
struct MemberFunctionClass<Return (Class::*)(Args...)> {
    using Type = Class;
};
template <typename Return, typename Class, typename... Args>
struct MemberFunctionClass<Return (Class::*)(Args...) const> {
    using Type = Class;
};
template <typename Return, typename Class, typename... Args>
struct MemberFunctionClass<Return (Class::*)(Args...) noexcept> {
    using Type = Class;
};
template <typename Return, typename Class, typename... Args>
struct MemberFunctionClass<Return (Class::*)(Args...) const noexcept> {
    using Type = Class;
};

} // namespace detail

/// Creates a read-only property backed by a no-argument member getter.
template <typename Getter>
    requires requires { typename detail::MemberFunctionClass<Getter>::Type; }
[[nodiscard]] auto property(std::string_view name, Getter getter) {
    using Class = typename detail::MemberFunctionClass<Getter>::Type;
    return GetterPropertyDescriptor<Class, Getter>{std::string(name), getter};
}

/// Creates a property backed by a no-argument getter and one-argument setter.
template <typename Getter, typename Setter>
    requires requires {
        typename detail::MemberFunctionClass<Getter>::Type;
        typename detail::MemberFunctionClass<Setter>::Type;
    }
[[nodiscard]] auto property(std::string_view name, Getter getter,
                            Setter setter) {
    using Class = typename detail::MemberFunctionClass<Getter>::Type;
    static_assert(
        std::same_as<Class, typename detail::MemberFunctionClass<Setter>::Type>,
        "property getter and setter must belong to the same type");
    return GetterSetterPropertyDescriptor<Class, Getter, Setter>{
        std::string(name), getter, setter};
}

/// Creates a descriptor for a non-overloaded member function.
template <typename Method>
[[nodiscard]] auto method(std::string_view name, Method methodPointer) {
    using Class = typename detail::MemberFunctionClass<Method>::Type;
    return MethodDescriptor<Class, Method>{std::string(name), methodPointer};
}

namespace detail {

class UserdataMember {
  public:
    enum class Kind { Property, Method };

    virtual ~UserdataMember() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Kind kind() const noexcept = 0;
    virtual int get(lua_State *state) = 0;
    virtual int set(lua_State *state) = 0;
    virtual int call(lua_State *state) = 0;
};

class UserdataConstructor {
  public:
    virtual ~UserdataConstructor() = default;
    virtual int create(lua_State *state, std::string_view metatableName) = 0;
};

class UserdataType {
  public:
    std::string name;
    std::string metatableName;
    std::vector<std::unique_ptr<UserdataMember>> members;
    std::unique_ptr<UserdataConstructor> constructor;

    [[nodiscard]] UserdataMember *find(std::string_view memberName) const {
        for (const auto &member : members) {
            if (member->name() == memberName) {
                return member.get();
            }
        }
        return nullptr;
    }
};

template <typename T> struct UserdataStorage {
    bool initialized = false;
    alignas(T) std::byte object[sizeof(T)];

    [[nodiscard]] T *get() noexcept {
        return std::launder(reinterpret_cast<T *>(object));
    }
};

template <typename T> void destroy_userdata(lua_State *, void *data) {
    auto *storage = static_cast<UserdataStorage<T> *>(data);
    if (storage->initialized) {
        storage->get()->~T();
        storage->initialized = false;
    }
}

inline void require_userdata_type(lua_State *state, int index,
                                  const UserdataType &type) {
    if (lua_type(state, index) != LUA_TUSERDATA ||
        !lua_getmetatable(state, index)) {
        throw std::invalid_argument("expected userdata of type '" + type.name +
                                    "'");
    }
    lua_getfield(state, -1, "__lunaris_type");
    const bool matches = lua_touserdata(state, -1) == &type;
    lua_pop(state, 2);
    if (!matches) {
        throw std::invalid_argument("expected userdata of type '" + type.name +
                                    "'");
    }
}

template <typename T>
[[nodiscard]] T *get_userdata(lua_State *state, int index,
                              const UserdataType &type) {
    require_userdata_type(state, index, type);
    auto *storage =
        static_cast<UserdataStorage<T> *>(lua_touserdata(state, index));
    if (storage == nullptr || !storage->initialized) {
        throw std::invalid_argument("userdata of type '" + type.name +
                                    "' is not initialized");
    }
    return storage->get();
}

template <typename T>
[[nodiscard]] T read_argument(lua_State *state, int index) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::same_as<U, bool>) {
        if (lua_type(state, index) != LUA_TBOOLEAN) {
            throw std::invalid_argument("expected a boolean argument");
        }
        return lua_toboolean(state, index) != 0;
    } else if constexpr (std::integral<U>) {
        std::int64_t value = 0;
        if (lua_type(state, index) == LUA_TINTEGER) {
            int isInteger = 0;
            value = lua_tointeger64(state, index, &isInteger);
            if (!isInteger) {
                throw std::invalid_argument(
                    "expected an in-range integer argument");
            }
        } else if (lua_type(state, index) == LUA_TNUMBER) {
            const double number = lua_tonumber(state, index);
            if (!std::isfinite(number) || std::trunc(number) != number ||
                number < -9223372036854775808.0 ||
                number >= 9223372036854775808.0) {
                throw std::invalid_argument(
                    "expected an in-range integer argument");
            }
            value = static_cast<std::int64_t>(number);
        } else {
            throw std::invalid_argument(
                "expected an in-range integer argument");
        }
        if (!std::in_range<U>(value)) {
            throw std::invalid_argument(
                "expected an in-range integer argument");
        }
        return static_cast<U>(value);
    } else if constexpr (std::floating_point<U>) {
        int isNumber = 0;
        const double value = lua_tonumberx(state, index, &isNumber);
        if (!isNumber) {
            throw std::invalid_argument("expected a number argument");
        }
        return static_cast<U>(value);
    } else if constexpr (std::same_as<U, std::string>) {
        if (lua_type(state, index) != LUA_TSTRING) {
            throw std::invalid_argument("expected a string argument");
        }
        std::size_t length = 0;
        const char *value = lua_tolstring(state, index, &length);
        return std::string(value, length);
    } else if constexpr (std::same_as<U, std::string_view>) {
        if (lua_type(state, index) != LUA_TSTRING) {
            throw std::invalid_argument("expected a string argument");
        }
        std::size_t length = 0;
        const char *value = lua_tolstring(state, index, &length);
        return std::string_view(value, length);
    } else {
        static_assert(!sizeof(U), "Unsupported C++ binding argument type");
    }
}

template <typename... Args, std::size_t... Indices>
[[nodiscard]] auto read_arguments(lua_State *state, int firstArgument,
                                  std::index_sequence<Indices...>) {
    return std::tuple<std::remove_cvref_t<Args>...>{read_argument<Args>(
        state, firstArgument + static_cast<int>(Indices))...};
}

inline int push_value(lua_State *state, std::nullptr_t) {
    lua_pushnil(state);
    return 1;
}

inline int push_value(lua_State *state, bool value) {
    lua_pushboolean(state, value);
    return 1;
}

template <std::integral T>
    requires(!std::same_as<std::remove_cv_t<T>, bool>)
int push_value(lua_State *state, T value) {
    if (!std::in_range<std::int64_t>(value)) {
        throw std::out_of_range("C++ integer return value exceeds Luau range");
    }
    lua_pushinteger64(state, static_cast<std::int64_t>(value));
    return 1;
}

template <std::floating_point T> int push_value(lua_State *state, T value) {
    lua_pushnumber(state, static_cast<double>(value));
    return 1;
}

inline int push_value(lua_State *state, std::string_view value) {
    lua_pushlstring(state, value.empty() ? "" : value.data(), value.size());
    return 1;
}

inline int push_value(lua_State *state, const std::string &value) {
    return push_value(state, std::string_view(value));
}

template <typename T> int push_return(lua_State *state, T &&value) {
    return push_value(state, std::forward<T>(value));
}

template <typename F> int invoke_protected(lua_State *state, F &&function) {
    int resultCount = 0;
    bool failed = false;
    {
        std::string error;
        bool caughtException = false;
        try {
            resultCount = std::forward<F>(function)();
        } catch (const std::exception &exception) {
            error = exception.what();
            caughtException = true;
        } catch (...) {
            error = "C++ binding threw a non-standard exception";
            caughtException = true;
        }
        if (caughtException) {
            if (error.empty()) {
                error = "C++ binding threw an exception";
            }
            lua_pushlstring(state, error.data(), error.size());
            failed = true;
        }
    }
    if (failed) {
        lua_error(state);
        return 0;
    }
    return resultCount;
}

inline int userdata_member_call(lua_State *state) {
    auto *member = static_cast<UserdataMember *>(
        lua_touserdata(state, lua_upvalueindex(1)));
    if (member == nullptr) {
        lua_pushstring(state, "invalid Lunaris userdata method");
        lua_error(state);
        return 0;
    }
    return invoke_protected(state,
                            [state, member] { return member->call(state); });
}

inline int userdata_index(lua_State *state) {
    auto *type =
        static_cast<UserdataType *>(lua_touserdata(state, lua_upvalueindex(1)));
    if (type == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    if (lua_type(state, 2) != LUA_TSTRING) {
        lua_pushnil(state);
        return 1;
    }
    std::size_t length = 0;
    const char *key = lua_tolstring(state, 2, &length);
    UserdataMember *member = type->find(std::string_view(key, length));
    if (member == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    if (member->kind() == UserdataMember::Kind::Method) {
        lua_pushlightuserdata(state, member);
        lua_pushcclosure(state, userdata_member_call, "Lunaris method", 1);
        return 1;
    }
    return invoke_protected(state,
                            [state, member] { return member->get(state); });
}

inline int userdata_newindex(lua_State *state) {
    auto *type =
        static_cast<UserdataType *>(lua_touserdata(state, lua_upvalueindex(1)));
    return invoke_protected(state, [state, type] {
        if (type == nullptr || lua_type(state, 2) != LUA_TSTRING) {
            throw std::invalid_argument("invalid userdata property");
        }
        std::size_t length = 0;
        const char *key = lua_tolstring(state, 2, &length);
        UserdataMember *member = type->find(std::string_view(key, length));
        if (member == nullptr ||
            member->kind() != UserdataMember::Kind::Property) {
            throw std::invalid_argument("unknown property on userdata '" +
                                        type->name + "'");
        }
        return member->set(state);
    });
}

inline int userdata_construct(lua_State *state) {
    auto *type =
        static_cast<UserdataType *>(lua_touserdata(state, lua_upvalueindex(1)));
    if (type == nullptr || !type->constructor) {
        lua_pushstring(state, "invalid Lunaris userdata constructor");
        lua_error(state);
        return 0;
    }
    return invoke_protected(state, [state, type] {
        return type->constructor->create(state, type->metatableName);
    });
}

template <typename T, typename... Args>
class DefaultUserdataConstructor final : public UserdataConstructor {
  public:
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "over-aligned C++ usertypes are not supported");

    int create(lua_State *state, std::string_view metatableName) override {
        if (lua_gettop(state) != static_cast<int>(sizeof...(Args))) {
            throw std::invalid_argument("constructor received the wrong number "
                                        "of arguments");
        }
        auto arguments = read_arguments<Args...>(
            state, 1, std::index_sequence_for<Args...>{});
        return std::apply(
            [state, metatableName](auto &&...values) {
                auto *storage = static_cast<UserdataStorage<T> *>(
                    lua_newuserdatadtor(state, sizeof(UserdataStorage<T>),
                                        destroy_userdata<T>));
                if (storage == nullptr) {
                    throw std::bad_alloc{};
                }
                new (storage) UserdataStorage<T>();
                new (storage->object)
                    T(std::forward<decltype(values)>(values)...);
                storage->initialized = true;
                const std::string key(metatableName);
                lua_getfield(state, LUA_REGISTRYINDEX, key.c_str());
                if (lua_type(state, -1) != LUA_TTABLE) {
                    throw std::runtime_error(
                        "registered userdata metatable is missing");
                }
                lua_setmetatable(state, -2);
                return 1;
            },
            std::move(arguments));
    }
};

template <typename Method> struct MethodTraits;
template <typename R, typename Class, typename... Args>
struct MethodTraits<R (Class::*)(Args...)> {
    using Return = R;
    using Arguments = std::tuple<Args...>;
};
template <typename R, typename Class, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const> {
    using Return = R;
    using Arguments = std::tuple<Args...>;
};
template <typename R, typename Class, typename... Args>
struct MethodTraits<R (Class::*)(Args...) noexcept> {
    using Return = R;
    using Arguments = std::tuple<Args...>;
};
template <typename R, typename Class, typename... Args>
struct MethodTraits<R (Class::*)(Args...) const noexcept> {
    using Return = R;
    using Arguments = std::tuple<Args...>;
};

template <typename T, typename Method>
class MemberMethod final : public UserdataMember {
  public:
    MemberMethod(std::string name, Method method, const UserdataType *type)
        : m_name(std::move(name)), m_method(method), m_type(type) {
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return m_name;
    }
    [[nodiscard]] Kind kind() const noexcept override {
        return Kind::Method;
    }
    int get(lua_State *) override {
        return 0;
    }
    int set(lua_State *) override {
        throw std::invalid_argument("methods cannot be assigned");
    }
    int call(lua_State *state) override {
        auto *object = get_userdata<T>(state, 1, *m_type);
        using Traits = MethodTraits<Method>;
        using Arguments = typename Traits::Arguments;
        if (lua_gettop(state) !=
            static_cast<int>(std::tuple_size_v<Arguments>) + 1) {
            throw std::invalid_argument("method '" + m_name +
                                        "' received the wrong number of "
                                        "arguments");
        }
        return invoke(state, object, static_cast<Arguments *>(nullptr));
    }

  private:
    template <typename... Args>
    int invoke(lua_State *state, T *object, std::tuple<Args...> *) {
        auto arguments = read_arguments<Args...>(
            state, 2, std::index_sequence_for<Args...>{});
        using Return = std::invoke_result_t<Method, T &, Args...>;
        if constexpr (std::is_void_v<Return>) {
            std::apply(
                [this, object](auto &&...values) {
                    std::invoke(m_method, *object,
                                std::forward<decltype(values)>(values)...);
                },
                std::move(arguments));
            return 0;
        } else {
            auto result = std::apply(
                [this, object](auto &&...values) {
                    return std::invoke(
                        m_method, *object,
                        std::forward<decltype(values)>(values)...);
                },
                std::move(arguments));
            return push_return(state, std::move(result));
        }
    }

    std::string m_name;
    Method m_method;
    const UserdataType *m_type;
};

template <typename T, typename Member>
class FieldProperty final : public UserdataMember {
  public:
    FieldProperty(std::string name, Member T::*member, const UserdataType *type)
        : m_name(std::move(name)), m_member(member), m_type(type) {
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return m_name;
    }
    [[nodiscard]] Kind kind() const noexcept override {
        return Kind::Property;
    }
    int get(lua_State *state) override {
        auto *object = get_userdata<T>(state, 1, *m_type);
        return push_return(state, object->*m_member);
    }
    int set(lua_State *state) override {
        if constexpr (std::is_const_v<Member>) {
            throw std::invalid_argument("property '" + m_name +
                                        "' is read-only");
        } else {
            auto *object = get_userdata<T>(state, 1, *m_type);
            object->*m_member = read_argument<Member>(state, 3);
            return 0;
        }
    }
    int call(lua_State *) override {
        throw std::logic_error("property is not callable");
    }

  private:
    std::string m_name;
    Member T::*m_member;
    const UserdataType *m_type;
};

template <typename T, typename Getter, typename Setter>
class AccessorProperty final : public UserdataMember {
  public:
    AccessorProperty(std::string name, Getter getter, const UserdataType *type)
        requires std::same_as<Setter, std::monostate>
        : m_name(std::move(name)), m_getter(getter), m_type(type) {
        validate_getter();
    }

    AccessorProperty(std::string name, Getter getter, Setter setter,
                     const UserdataType *type)
        requires(!std::same_as<Setter, std::monostate>)
        : m_name(std::move(name)), m_getter(getter), m_setter(setter),
          m_type(type) {
        validate_getter();
        using SetterTraits = MethodTraits<Setter>;
        static_assert(std::tuple_size_v<typename SetterTraits::Arguments> == 1,
                      "property setter must take exactly one argument");
        static_assert(std::is_void_v<typename SetterTraits::Return>,
                      "property setter must return void");
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return m_name;
    }
    [[nodiscard]] Kind kind() const noexcept override {
        return Kind::Property;
    }
    int get(lua_State *state) override {
        auto *object = get_userdata<T>(state, 1, *m_type);
        decltype(auto) value = std::invoke(m_getter, *object);
        return push_return(state, std::forward<decltype(value)>(value));
    }
    int set(lua_State *state) override {
        if constexpr (std::same_as<Setter, std::monostate>) {
            throw std::invalid_argument("property '" + m_name +
                                        "' is read-only");
        } else {
            if (lua_gettop(state) != 3) {
                throw std::invalid_argument("property setter '" + m_name +
                                            "' received the wrong number of "
                                            "arguments");
            }
            auto *object = get_userdata<T>(state, 1, *m_type);
            using Arguments = typename MethodTraits<Setter>::Arguments;
            using Argument = std::tuple_element_t<0, Arguments>;
            auto value = read_argument<Argument>(state, 3);
            std::invoke(m_setter, *object, std::move(value));
            return 0;
        }
    }
    int call(lua_State *) override {
        throw std::logic_error("property is not callable");
    }

  private:
    static void validate_getter() {
        using GetterTraits = MethodTraits<Getter>;
        static_assert(std::tuple_size_v<typename GetterTraits::Arguments> == 0,
                      "property getter must take no arguments");
        static_assert(!std::is_void_v<typename GetterTraits::Return>,
                      "property getter must return a value");
    }

    std::string m_name;
    Getter m_getter;
    [[no_unique_address]] Setter m_setter;
    const UserdataType *m_type;
};

template <typename T, typename Class, typename Member>
void add_descriptor(const std::shared_ptr<UserdataType> &type,
                    PropertyDescriptor<Class, Member> descriptor) {
    static_assert(std::same_as<T, Class>,
                  "property belongs to a different usertype");
    if (type->find(descriptor.name) != nullptr) {
        throw std::invalid_argument("duplicate usertype member '" +
                                    descriptor.name + "'");
    }
    type->members.push_back(std::make_unique<FieldProperty<T, Member>>(
        std::move(descriptor.name), descriptor.member, type.get()));
}

template <typename T, typename Class, typename Getter>
void add_descriptor(const std::shared_ptr<UserdataType> &type,
                    GetterPropertyDescriptor<Class, Getter> descriptor) {
    static_assert(std::same_as<T, Class>,
                  "property belongs to a different usertype");
    if (type->find(descriptor.name) != nullptr) {
        throw std::invalid_argument("duplicate usertype member '" +
                                    descriptor.name + "'");
    }
    type->members.push_back(
        std::make_unique<AccessorProperty<T, Getter, std::monostate>>(
            std::move(descriptor.name), descriptor.getter, type.get()));
}

template <typename T, typename Class, typename Getter, typename Setter>
void add_descriptor(
    const std::shared_ptr<UserdataType> &type,
    GetterSetterPropertyDescriptor<Class, Getter, Setter> descriptor) {
    static_assert(std::same_as<T, Class>,
                  "property belongs to a different usertype");
    if (type->find(descriptor.name) != nullptr) {
        throw std::invalid_argument("duplicate usertype member '" +
                                    descriptor.name + "'");
    }
    type->members.push_back(
        std::make_unique<AccessorProperty<T, Getter, Setter>>(
            std::move(descriptor.name), descriptor.getter, descriptor.setter,
            type.get()));
}

template <typename T, typename Class, typename Method>
void add_descriptor(const std::shared_ptr<UserdataType> &type,
                    MethodDescriptor<Class, Method> descriptor) {
    static_assert(std::same_as<T, Class>,
                  "method belongs to a different usertype");
    if (type->find(descriptor.name) != nullptr) {
        throw std::invalid_argument("duplicate usertype member '" +
                                    descriptor.name + "'");
    }
    type->members.push_back(std::make_unique<MemberMethod<T, Method>>(
        std::move(descriptor.name), descriptor.method, type.get()));
}

template <typename T, typename ConstructorType, typename... Args>
void add_descriptor(const std::shared_ptr<UserdataType> &type,
                    ConstructorDescriptor<ConstructorType, Args...>) {
    static_assert(std::same_as<T, ConstructorType>,
                  "constructor belongs to a different usertype");
    if (type->constructor) {
        throw std::invalid_argument(
            "only one usertype constructor is supported");
    }
    type->constructor =
        std::make_unique<DefaultUserdataConstructor<T, Args...>>();
}

template <typename T, typename... Descriptors>
void register_usertype(lua_State *state,
                       const std::shared_ptr<StateLifetime> &lifetime,
                       std::string_view name, Descriptors &&...descriptors) {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "usertype name must be non-empty and contain no null byte");
    }
    if (lifetime == nullptr || lifetime->state != state) {
        throw std::logic_error("usertype State is not available");
    }

    const std::string typeName(name);
    lua_getglobal(state, typeName.c_str());
    const bool globalExists = !lua_isnil(state, -1);
    lua_pop(state, 1);
    if (globalExists) {
        throw std::invalid_argument("global '" + typeName +
                                    "' is already registered");
    }

    auto type = std::make_shared<UserdataType>();
    type->name = typeName;
    type->metatableName = "lunaris.usertype." + typeName;
    (add_descriptor<T>(type, std::forward<Descriptors>(descriptors)), ...);

    const int created = luaL_newmetatable(state, type->metatableName.c_str());
    if (created == 0) {
        lua_pop(state, 1);
        throw std::invalid_argument("usertype '" + typeName +
                                    "' is already registered");
    }
    const int metatable = lua_gettop(state);
    lua_pushlightuserdata(state, type.get());
    lua_setfield(state, metatable, "__lunaris_type");
    lua_pushlstring(state, typeName.data(), typeName.size());
    lua_setfield(state, metatable, "__type");
    lua_pushlightuserdata(state, type.get());
    lua_pushcclosure(state, userdata_index, "Lunaris __index", 1);
    lua_setfield(state, metatable, "__index");
    lua_pushlightuserdata(state, type.get());
    lua_pushcclosure(state, userdata_newindex, "Lunaris __newindex", 1);
    lua_setfield(state, metatable, "__newindex");
    lua_pushstring(state, "protected Lunaris usertype");
    lua_setfield(state, metatable, "__metatable");
    lua_setreadonly(state, metatable, true);
    lua_pop(state, 1);

    lifetime->bindingRegistrations.push_back(type);
    lua_createtable(state, 0, 1);
    if (type->constructor) {
        lua_pushlightuserdata(state, type.get());
        lua_pushcclosure(state, userdata_construct, "Lunaris constructor", 1);
        lua_setfield(state, -2, "new");
    }
    lua_setglobal(state, typeName.c_str());
}

} // namespace detail
} // namespace lunaris
