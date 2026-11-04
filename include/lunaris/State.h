#pragma once

#include "lunaris/Binding.h"
#include "lunaris/Bytecode.h"
#include "lunaris/BytecodeCache.h"
#include "lunaris/Library.h"
#include "lunaris/ModuleLoader.h"
#include "lunaris/Result.h"
#include "lunaris/Script.h"
#include "lunaris/StateConfig.h"
#include "lunaris/Table.h"
#include "lunaris/Value.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

struct lua_State;

namespace lunaris {

/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    /// Creates a Luau state with the supplied configuration.
    /// @throws std::bad_alloc if the state cannot be created or its initial
    /// allocations exceed the configured memory limit.
    explicit State(const StateConfig &config = {});

    /// Closes the owned Luau state, if any.
    ~State();

    State(const State &) = delete;
    State &operator=(const State &) = delete;

    /// Transfers state ownership from \p other.
    State(State &&other) noexcept;

    /// Closes the current state and transfers ownership from \p other.
    State &operator=(State &&other) noexcept;

    /// Returns the underlying Luau state without transferring ownership.
    /// Returns nullptr after the State has been moved from.
    [[nodiscard]] lua_State *native_handle() noexcept;

    /// Returns the number of values currently on the Luau stack.
    [[nodiscard]] int top() const;

    /// Removes \p count values from the top of the stack.
    /// @throws std::invalid_argument if count is negative.
    /// @throws std::out_of_range if count exceeds the stack height.
    void pop(int count = 1);

    /// Removes all values from the Luau stack.
    void clear();

    /// Pushes nil onto the Luau stack.
    void push(std::nullptr_t);

    /// Pushes a boolean onto the Luau stack.
    void push(bool value);

    /// Pushes a floating-point number onto the Luau stack.
    void push(double value);

    /// Pushes a string onto the Luau stack.
    void push(std::string_view value);

    /// Pushes a null-terminated string onto the Luau stack.
    /// @throws std::invalid_argument if value is nullptr.
    void push(const char *value) {
        if (value == nullptr) {
            throw std::invalid_argument("string value cannot be nullptr");
        }
        push(std::string_view(value));
    }

    /// Pushes a string onto the Luau stack.
    void push(const std::string &value) {
        push(std::string_view(value));
    }

    /// Pushes an owned scalar value onto the Luau stack.
    void push(const Value &value);

    /// Pushes a non-owning C++ object as userdata. The object must outlive
    /// this State and every Luau reference to the userdata.
    template <typename T>
    void push_borrowed(std::string_view typeName, T &object) {
        push_external_userdata(typeName, &object, {});
    }

    /// Pushes a shared C++ object as userdata. The userdata retains ownership
    /// until Luau collects it or this State closes.
    template <typename T>
    void push_shared(std::string_view typeName, std::shared_ptr<T> object) {
        if (object == nullptr) {
            throw std::invalid_argument("shared userdata cannot be null");
        }
        T *pointer = object.get();
        push_external_userdata(typeName, pointer, std::move(object));
    }

    /// Creates a new table and returns a registry-backed handle to it.
    [[nodiscard]] Table create_table();

    /// Creates a named library table in the global environment.
    /// The table remains read-only to Luau scripts as entries are registered.
    [[nodiscard]] Library create_library(std::string_view name);

    /// Creates a registry-backed handle for a table at \p index.
    /// @throws std::out_of_range if index is invalid.
    /// @throws std::invalid_argument if the value is not a table.
    [[nodiscard]] Table get_table(int index);

    /// Reads a scalar global; a missing global is returned as nil.
    /// @return The scalar value, or a Type error if the global is a reference
    /// value or the name contains an embedded null byte.
    [[nodiscard]] Result<Value> get_global(std::string_view name) const;

    /// Sets a scalar global. Setting nil removes the global.
    /// @return Success, or a Type error if the name contains an embedded null
    /// byte.
    [[nodiscard]] Result<void> set_global(std::string_view name,
                                          const Value &value);

    /// Compiles and loads a source chunk into this State.
    /// The returned script has its own global environment, falling back to
    /// State globals for reads, and can be executed more than once.
    /// @return A Script, or a Compile, Load, Memory, or Cache error.
    /// @throws std::logic_error if this State has been moved from.
    [[nodiscard]] Result<Script> load_script(std::string_view source,
                                             std::string_view chunkName);

    /// Sets the compiler options used by subsequent load_script calls.
    /// @throws std::invalid_argument if an option is outside its supported
    /// range.
    void set_compiler_options(CompilerOptions options);

    /// Installs optional bytecode cache storage; passing nullptr disables it.
    void
    set_bytecode_cache_storage(std::shared_ptr<BytecodeCacheStorage> storage);

    /// Loads owned Luau bytecode into this State as a Script with a private
    /// environment that falls back to State globals for reads.
    /// Bytecode format and version errors are returned as Load errors.
    /// @throws std::logic_error if this State has been moved from.
    [[nodiscard]] Result<Script> load_bytecode(const Bytecode &bytecode,
                                               std::string_view chunkName);

    /// Installs or replaces the provider used by Luau require(...).
    /// Passing nullptr disables module loading and clears cached modules.
    void set_module_loader(std::shared_ptr<ModuleLoader> moduleLoader);

    /// Registers an owned, State-local C++ usertype in Luau.
    /// Supported members are data properties and non-overloaded member
    /// functions using scalar arguments and return values.
    /// A constructor<T, Args...>() descriptor adds Type.new(...).
    template <typename T, typename... Descriptors>
    void new_usertype(std::string_view name, Descriptors &&...descriptors) {
        detail::register_usertype<T>(checked_state(), m_lifetime, name,
                                     std::forward<Descriptors>(descriptors)...);
    }

    /// Registers an immutable Luau table containing enum member values.
    template <typename Enum>
    void
    new_enum(std::string_view name,
             std::initializer_list<std::pair<std::string_view, Enum>> values) {
        static_assert(std::is_enum_v<Enum>,
                      "new_enum requires a C++ enum type");
        detail::register_named_values(checked_state(), name, values);
    }

    /// Registers an immutable Luau table containing bit-flag values.
    template <typename Enum>
    void
    new_flags(std::string_view name,
              std::initializer_list<std::pair<std::string_view, Enum>> values) {
        static_assert(std::is_enum_v<Enum>,
                      "new_flags requires a C++ enum type");
        detail::register_named_values(checked_state(), name, values);
    }

    /// Invalidates a module and all cached modules that depend on it.
    /// Invalidated modules are loaded again on their next require(...).
    /// @return Success, or a Module error if resolution fails or a target is
    /// currently loading.
    [[nodiscard]] Result<void> invalidate_module(std::string_view name);

    /// Invalidates a module and its dependents, then immediately reloads it.
    /// Dependent modules are reloaded lazily on their next require(...).
    /// @return Success, or a Module error if resolution or execution fails.
    [[nodiscard]] Result<void> reload_module(std::string_view name);

    /// Pushes an integer onto the Luau stack.
    template <std::integral T>
        requires(!std::same_as<std::remove_cv_t<T>, bool>)
    void push(T value) {
        if (!std::in_range<std::int64_t>(value)) {
            throw std::out_of_range("integer value is outside Luau's range");
        }
        push(static_cast<std::int64_t>(value));
    }

    /// Pushes a 64-bit integer onto the Luau stack.
    void push(std::int64_t value);

    /// Reads a scalar value from a stack index without removing it.
    /// Integer reads require a Luau integer; floating-point reads accept both
    /// Luau integer and number values. String and boolean reads are exact.
    /// @throws std::out_of_range if index is invalid.
    /// @throws std::invalid_argument if the stack value has an incompatible
    /// type.
    template <typename T> [[nodiscard]] T get(int index) const {
        validate_index(index);

        if constexpr (std::same_as<T, Value>) {
            return get_value(index);
        } else if constexpr (std::same_as<T, bool>) {
            return get_boolean(index);
        } else if constexpr (std::integral<T>) {
            const std::int64_t value = get_integer(index);
            if (!std::in_range<T>(value)) {
                throw std::out_of_range(
                    "Luau integer is outside the target range");
            }
            return static_cast<T>(value);
        } else if constexpr (std::same_as<T, double>) {
            return get_number(index);
        } else if constexpr (std::same_as<T, std::string>) {
            return get_string(index);
        } else {
            static_assert(!sizeof(T), "Unsupported type for State::get");
        }
    }

  private:
    struct AllocatorContext;

    [[nodiscard]] lua_State *checked_state() const;
    template <typename T>
    void push_external_userdata(std::string_view typeName, T *object,
                                std::shared_ptr<T> sharedObject) {
        lua_State *state = checked_state();
        const int stackTop = lua_gettop(state);
        try {
            detail::push_external_userdata<T>(state, m_lifetime, typeName,
                                              object, std::move(sharedObject));
        } catch (...) {
            lua_settop(state, stackTop);
            throw;
        }
    }
    void validate_index(int index) const;
    [[nodiscard]] bool get_boolean(int index) const;
    [[nodiscard]] std::int64_t get_integer(int index) const;
    [[nodiscard]] double get_number(int index) const;
    [[nodiscard]] std::string get_string(int index) const;
    [[nodiscard]] Value get_value(int index) const;
    static void *tracked_allocate(void *userData, void *pointer,
                                  std::size_t oldSize, std::size_t newSize);

    lua_State *m_state = nullptr;
    std::unique_ptr<AllocatorContext> m_allocatorContext;
    std::shared_ptr<detail::StateLifetime> m_lifetime;
};

} // namespace lunaris
