#pragma once

#include "lunaris/Binding.h"
#include "lunaris/Value.h"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lunaris {

/// Controls whether a registered library module is also available globally.
enum class LibraryVisibility { GlobalAndModule, ModuleOnly };

namespace detail {

class LibraryFunction {
  public:
    virtual ~LibraryFunction() = default;
    virtual int invoke(lua_State *state) = 0;
};

template <typename Return, typename... Args>
class LibraryFunctionPointer final : public LibraryFunction {
  public:
    using Function = Return (*)(Args...);

    explicit LibraryFunctionPointer(Function function) : m_function(function) {
    }

    int invoke(lua_State *state) override {
        if (lua_gettop(state) != static_cast<int>(sizeof...(Args))) {
            throw std::invalid_argument("library function received the wrong "
                                        "number of arguments");
        }
        auto arguments = read_arguments<Args...>(
            state, 1, std::index_sequence_for<Args...>{});
        if constexpr (std::is_void_v<Return>) {
            std::apply(m_function, std::move(arguments));
            return 0;
        } else {
            return push_return(state,
                               std::apply(m_function, std::move(arguments)));
        }
    }

  private:
    Function m_function;
};

inline int library_function_call(lua_State *state) {
    auto *function = static_cast<LibraryFunction *>(
        lua_touserdata(state, lua_upvalueindex(1)));
    if (function == nullptr) {
        lua_pushstring(state, "invalid Lunaris library function");
        lua_error(state);
        return 0;
    }
    return invoke_protected(
        state, [state, function] { return function->invoke(state); });
}

inline void ensure_library_field_available(lua_State *state, int table,
                                           std::string_view name) {
    const int absoluteTable = lua_absindex(state, table);
    lua_getfield(state, absoluteTable, std::string(name).c_str());
    const bool exists = !lua_isnil(state, -1);
    lua_pop(state, 1);
    if (exists) {
        throw std::invalid_argument("library field '" + std::string(name) +
                                    "' is already registered");
    }
}

} // namespace detail

/// Handle for a named, immutable Luau library table.
class Library {
  public:
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    Library(Library &&other) noexcept;
    Library &operator=(Library &&other) noexcept;
    ~Library();

    /// Adds a scalar constant to the library.
    void set_constant(std::string_view name, const Value &value);

    /// Registers this library as a stable require(moduleName) module.
    /// The module name must be unique within its State. ModuleOnly also
    /// removes the library's global name if it still refers to this table.
    void register_module(
        std::string_view moduleName,
        LibraryVisibility visibility = LibraryVisibility::GlobalAndModule);

    /// Pushes a non-owning object registered as a library usertype.
    template <typename T>
    void push_borrowed(std::string_view typeName, T &object) {
        push_external_userdata(typeName, &object, {});
    }

    /// Pushes a shared object registered as a library usertype.
    template <typename T>
    void push_shared(std::string_view typeName, std::shared_ptr<T> object) {
        if (object == nullptr) {
            throw std::invalid_argument("shared userdata cannot be null");
        }
        T *pointer = object.get();
        push_external_userdata(typeName, pointer, std::move(object));
    }

    /// Registers a non-overloaded free function in the library.
    template <typename Return, typename... Args>
    void set_function(std::string_view name, Return (*function)(Args...)) {
        validate_name(name);
        if (function == nullptr) {
            throw std::invalid_argument("library function cannot be null");
        }
        auto registered =
            std::make_shared<detail::LibraryFunctionPointer<Return, Args...>>(
                function);
        with_table([&](lua_State *state, int table) {
            detail::ensure_library_field_available(state, table, name);
            checked_lifetime()->bindingRegistrations.push_back(registered);
            lua_pushlightuserdata(state, registered.get());
            lua_pushcclosure(state, detail::library_function_call,
                             "Lunaris library function", 1);
            lua_setreadonly(state, table, false);
            lua_setfield(state, table, std::string(name).c_str());
            lua_setreadonly(state, table, true);
        });
    }

    /// Registers an owned usertype inside this library.
    template <typename T, typename... Descriptors>
    void new_usertype(std::string_view name, Descriptors &&...descriptors) {
        const std::string qualifiedName = qualified_type_name(name);
        with_table([&](lua_State *state, int table) {
            detail::register_usertype_in_table<T>(
                state, checked_lifetime(), qualifiedName, name, table,
                std::forward<Descriptors>(descriptors)...);
        });
    }

    /// Registers an immutable enum table inside this library.
    template <typename Enum>
    void
    new_enum(std::string_view name,
             std::initializer_list<std::pair<std::string_view, Enum>> values) {
        static_assert(std::is_enum_v<Enum>,
                      "new_enum requires a C++ enum type");
        with_table([&](lua_State *state, int table) {
            detail::register_named_values_in_table(state, table, name, values);
        });
    }

    /// Registers an immutable flag table inside this library.
    template <typename Enum>
    void
    new_flags(std::string_view name,
              std::initializer_list<std::pair<std::string_view, Enum>> values) {
        static_assert(std::is_enum_v<Enum>,
                      "new_flags requires a C++ enum type");
        with_table([&](lua_State *state, int table) {
            detail::register_named_values_in_table(state, table, name, values);
        });
    }

  private:
    friend class State;
    Library(std::weak_ptr<detail::StateLifetime> lifetime, int reference,
            std::string name);

    [[nodiscard]] std::shared_ptr<detail::StateLifetime>
    checked_lifetime() const;
    static void validate_name(std::string_view name);
    [[nodiscard]] std::string qualified_type_name(std::string_view name) const;
    void release() noexcept;

    template <typename T>
    void push_external_userdata(std::string_view typeName, T *object,
                                std::shared_ptr<T> sharedObject) {
        const std::string qualifiedName = qualified_type_name(typeName);
        auto lifetime = checked_lifetime();
        lua_State *state = lifetime->state;
        const int stackTop = lua_gettop(state);
        try {
            detail::push_external_userdata<T>(state, lifetime, qualifiedName,
                                              object, std::move(sharedObject));
        } catch (...) {
            lua_settop(state, stackTop);
            throw;
        }
    }

    template <typename Function> void with_table(Function &&function) {
        auto lifetime = checked_lifetime();
        lua_State *state = lifetime->state;
        const int stackTop = lua_gettop(state);
        try {
            lua_getref(state, m_reference);
            if (lua_type(state, -1) != LUA_TTABLE) {
                throw std::runtime_error("registered Luau library is missing");
            }
            const int table = lua_absindex(state, -1);
            std::forward<Function>(function)(state, table);
            lua_settop(state, stackTop);
        } catch (...) {
            lua_settop(state, stackTop);
            throw;
        }
    }

    std::weak_ptr<detail::StateLifetime> m_lifetime;
    int m_reference = -1;
    std::string m_name;
};

} // namespace lunaris
