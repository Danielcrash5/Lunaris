#include "lunaris/SharedTable.h"

#include "lunaris/Binding.h"
#include "lunaris/State.h"

#include <lua.h>
#include <lualib.h>

#include <concepts>
#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace lunaris {
namespace detail {

using SharedStoredValue = std::variant<bool, std::int64_t, double, std::string,
                                       std::shared_ptr<SharedTableData>>;

struct SharedTableData {
    mutable std::mutex mutex;
    std::unordered_map<std::string, SharedStoredValue> values;
};

struct SharedTableRegistryData {
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<SharedTableData>> tables;
};

struct SharedTableBindings {
    std::shared_ptr<SharedTableRegistryData> registry;
};

struct SharedTableUserdata {
    std::shared_ptr<SharedTableData> data;
};

} // namespace detail

namespace {

constexpr char kTypeMarker = 0;
constexpr const char *kSharedTableMetatable =
    "lunaris.SharedTable.metatable.v1";

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

void validate_name(std::string_view name, std::string_view description) {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string(description) +
                                    " must be non-empty and contain no null "
                                    "byte");
    }
}

void destroy_shared_table(lua_State *, void *data) {
    static_cast<detail::SharedTableUserdata *>(data)->~SharedTableUserdata();
}

detail::SharedTableUserdata *get_shared_table_userdata(lua_State *state,
                                                       int index) {
    if (lua_type(state, index) != LUA_TUSERDATA ||
        !lua_getmetatable(state, index)) {
        throw std::invalid_argument("expected a SharedTable");
    }
    lua_getfield(state, -1, "__lunaris_shared_table");
    const bool matches = lua_touserdata(state, -1) == &kTypeMarker;
    lua_pop(state, 2);
    if (!matches) {
        throw std::invalid_argument("expected a SharedTable");
    }
    auto *userdata = static_cast<detail::SharedTableUserdata *>(
        lua_touserdata(state, index));
    if (userdata == nullptr || userdata->data == nullptr) {
        throw std::invalid_argument("SharedTable is not initialized");
    }
    return userdata;
}

void push_shared_table(lua_State *state,
                       std::shared_ptr<detail::SharedTableData> data) {
    auto *userdata =
        static_cast<detail::SharedTableUserdata *>(lua_newuserdatadtor(
            state, sizeof(detail::SharedTableUserdata), destroy_shared_table));
    if (userdata == nullptr) {
        throw std::bad_alloc{};
    }
    new (userdata) detail::SharedTableUserdata{std::move(data)};
    lua_getfield(state, LUA_REGISTRYINDEX, kSharedTableMetatable);
    if (lua_type(state, -1) != LUA_TTABLE) {
        throw std::runtime_error("SharedTable metatable is not installed");
    }
    lua_setmetatable(state, -2);
}

std::string read_key(lua_State *state, int index) {
    if (lua_type(state, index) != LUA_TSTRING) {
        throw std::invalid_argument("SharedTable keys must be strings");
    }
    std::size_t length = 0;
    const char *key = lua_tolstring(state, index, &length);
    return std::string(key, length);
}

detail::SharedStoredValue read_stored_value(lua_State *state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0;
    case LUA_TINTEGER: {
        int isInteger = 0;
        const std::int64_t value = lua_tointeger64(state, index, &isInteger);
        if (!isInteger) {
            throw std::invalid_argument("SharedTable integer is out of range");
        }
        return value;
    }
    case LUA_TNUMBER: {
        int isNumber = 0;
        const double value = lua_tonumberx(state, index, &isNumber);
        if (!isNumber) {
            throw std::invalid_argument("SharedTable value must be a number");
        }
        return value;
    }
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char *value = lua_tolstring(state, index, &length);
        return std::string(value, length);
    }
    case LUA_TUSERDATA:
        return get_shared_table_userdata(state, index)->data;
    default:
        throw std::invalid_argument(
            "SharedTable values must be booleans, numbers, strings, or "
            "SharedTables");
    }
}

int push_stored_value(lua_State *state,
                      const detail::SharedStoredValue &value) {
    std::visit(
        [state](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<T, bool>) {
                lua_pushboolean(state, item);
            } else if constexpr (std::same_as<T, std::int64_t>) {
                lua_pushinteger64(state, item);
            } else if constexpr (std::same_as<T, double>) {
                lua_pushnumber(state, item);
            } else if constexpr (std::same_as<T, std::string>) {
                lua_pushlstring(state, item.data(), item.size());
            } else {
                push_shared_table(state, item);
            }
        },
        value);
    return 1;
}

void set_value(lua_State *state, int tableIndex, int keyIndex, int valueIndex) {
    auto *userdata = get_shared_table_userdata(state, tableIndex);
    const std::string key = read_key(state, keyIndex);
    if (lua_type(state, valueIndex) == LUA_TNIL) {
        std::lock_guard lock(userdata->data->mutex);
        userdata->data->values.erase(key);
        return;
    }
    auto value = read_stored_value(state, valueIndex);
    std::lock_guard lock(userdata->data->mutex);
    userdata->data->values.insert_or_assign(key, std::move(value));
}

int shared_table_new(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        if (lua_gettop(state) != 0) {
            throw std::invalid_argument("SharedTable.new expects no arguments");
        }
        push_shared_table(state, std::make_shared<detail::SharedTableData>());
        return 1;
    });
}

int shared_table_get(lua_State *state);
int shared_table_set(lua_State *state);
int shared_table_remove(lua_State *state);
int shared_table_count(lua_State *state);

int shared_table_index(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        auto *userdata = get_shared_table_userdata(state, 1);
        const std::string key = read_key(state, 2);
        if (key == "Get") {
            lua_pushcclosure(state, shared_table_get, "SharedTable.Get", 0);
            return 1;
        }
        if (key == "Set") {
            lua_pushcclosure(state, shared_table_set, "SharedTable.Set", 0);
            return 1;
        }
        if (key == "Remove") {
            lua_pushcclosure(state, shared_table_remove, "SharedTable.Remove",
                             0);
            return 1;
        }
        if (key == "Count") {
            lua_pushcclosure(state, shared_table_count, "SharedTable.Count", 0);
            return 1;
        }

        std::lock_guard lock(userdata->data->mutex);
        const auto iterator = userdata->data->values.find(key);
        if (iterator == userdata->data->values.end()) {
            lua_pushnil(state);
            return 1;
        }
        return push_stored_value(state, iterator->second);
    });
}

int shared_table_newindex(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        set_value(state, 1, 2, 3);
        return 0;
    });
}

int shared_table_get(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        auto *userdata = get_shared_table_userdata(state, 1);
        const std::string key = read_key(state, 2);
        std::lock_guard lock(userdata->data->mutex);
        const auto iterator = userdata->data->values.find(key);
        if (iterator == userdata->data->values.end()) {
            lua_pushnil(state);
            return 1;
        }
        return push_stored_value(state, iterator->second);
    });
}

int shared_table_set(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        if (lua_gettop(state) != 3) {
            throw std::invalid_argument(
                "SharedTable:Set expects a key and value");
        }
        set_value(state, 1, 2, 3);
        return 0;
    });
}

int shared_table_remove(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        auto *userdata = get_shared_table_userdata(state, 1);
        const std::string key = read_key(state, 2);
        std::lock_guard lock(userdata->data->mutex);
        lua_pushboolean(state, userdata->data->values.erase(key) != 0);
        return 1;
    });
}

int shared_table_count(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        auto *userdata = get_shared_table_userdata(state, 1);
        std::lock_guard lock(userdata->data->mutex);
        lua_pushinteger64(
            state, static_cast<std::int64_t>(userdata->data->values.size()));
        return 1;
    });
}

detail::SharedTableBindings *get_bindings(lua_State *state) {
    auto *bindings = static_cast<detail::SharedTableBindings *>(
        lua_touserdata(state, lua_upvalueindex(1)));
    if (bindings == nullptr) {
        throw std::logic_error("SharedTableRegistry is not initialized");
    }
    return bindings;
}

int registry_set(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        auto *bindings = get_bindings(state);
        if (lua_gettop(state) != 3) {
            throw std::invalid_argument(
                "SetSharedTable expects a name and table or nil");
        }
        const std::string name = read_key(state, 2);
        validate_name(name, "SharedTableRegistry name");

        std::lock_guard lock(bindings->registry->mutex);
        if (lua_type(state, 3) == LUA_TNIL) {
            bindings->registry->tables.erase(name);
        } else {
            bindings->registry->tables.insert_or_assign(
                name, get_shared_table_userdata(state, 3)->data);
        }
        return 0;
    });
}

int registry_get(lua_State *state) {
    return detail::invoke_protected(state, [state] {
        auto *bindings = get_bindings(state);
        if (lua_gettop(state) != 2) {
            throw std::invalid_argument("GetSharedTable expects a name");
        }
        const std::string name = read_key(state, 2);
        std::shared_ptr<detail::SharedTableData> table;
        {
            std::lock_guard lock(bindings->registry->mutex);
            const auto iterator = bindings->registry->tables.find(name);
            if (iterator == bindings->registry->tables.end()) {
                lua_pushnil(state);
                return 1;
            }
            table = iterator->second;
        }
        push_shared_table(state, std::move(table));
        return 1;
    });
}

void install_table_metatable(lua_State *state) {
    if (luaL_newmetatable(state, kSharedTableMetatable) == 0) {
        lua_pop(state, 1);
        throw std::invalid_argument(
            "SharedTable is already installed in State");
    }
    const int metatable = lua_gettop(state);
    lua_pushlightuserdata(state, const_cast<char *>(&kTypeMarker));
    lua_setfield(state, metatable, "__lunaris_shared_table");
    lua_pushcclosure(state, shared_table_index, "SharedTable.__index", 0);
    lua_setfield(state, metatable, "__index");
    lua_pushcclosure(state, shared_table_newindex, "SharedTable.__newindex", 0);
    lua_setfield(state, metatable, "__newindex");
    lua_pushstring(state, "protected SharedTable");
    lua_setfield(state, metatable, "__metatable");
    lua_setreadonly(state, metatable, true);
    lua_pop(state, 1);
}

} // namespace

SharedTable::SharedTable(std::shared_ptr<detail::SharedTableData> data)
    : m_data(std::move(data)) {
}

std::optional<Value> SharedTable::get(std::string_view key) const {
    if (m_data == nullptr) {
        throw std::logic_error("operation on an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    const auto iterator = m_data->values.find(std::string(key));
    if (iterator == m_data->values.end()) {
        return std::nullopt;
    }
    return std::visit(
        [](const auto &value) -> std::optional<Value> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<
                              T, std::shared_ptr<detail::SharedTableData>>) {
                throw std::invalid_argument(
                    "SharedTable value is nested; use get_table()");
            } else {
                return Value{value};
            }
        },
        iterator->second);
}

std::optional<SharedTable> SharedTable::get_table(std::string_view key) const {
    if (m_data == nullptr) {
        throw std::logic_error("operation on an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    const auto iterator = m_data->values.find(std::string(key));
    if (iterator == m_data->values.end()) {
        return std::nullopt;
    }
    const auto *nested = std::get_if<std::shared_ptr<detail::SharedTableData>>(
        &iterator->second);
    if (nested == nullptr) {
        return std::nullopt;
    }
    return SharedTable(*nested);
}

void SharedTable::set(std::string_view key, const Value &value) const {
    if (m_data == nullptr) {
        throw std::logic_error("operation on an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    if (std::holds_alternative<std::monostate>(value)) {
        m_data->values.erase(std::string(key));
        return;
    }
    std::visit(
        [this, key](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (!std::same_as<T, std::monostate>) {
                m_data->values.insert_or_assign(std::string(key), item);
            }
        },
        value);
}

void SharedTable::set(std::string_view key, const SharedTable &value) const {
    if (m_data == nullptr || value.m_data == nullptr) {
        throw std::logic_error("cannot store an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    m_data->values.insert_or_assign(std::string(key), value.m_data);
}

bool SharedTable::remove(std::string_view key) const {
    if (m_data == nullptr) {
        throw std::logic_error("operation on an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    return m_data->values.erase(std::string(key)) != 0;
}

std::size_t SharedTable::size() const {
    if (m_data == nullptr) {
        throw std::logic_error("operation on an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    return m_data->values.size();
}

SharedTable::operator bool() const noexcept {
    return m_data != nullptr;
}

SharedTableRegistry::SharedTableRegistry()
    : m_data(std::make_shared<detail::SharedTableRegistryData>()) {
}

SharedTable SharedTableRegistry::create_table() const {
    return SharedTable(std::make_shared<detail::SharedTableData>());
}

void SharedTableRegistry::set_shared_table(std::string_view name,
                                           std::optional<SharedTable> table) {
    validate_name(name, "SharedTableRegistry name");
    if (table.has_value() && !*table) {
        throw std::invalid_argument("cannot register an empty SharedTable");
    }
    std::lock_guard lock(m_data->mutex);
    if (!table.has_value()) {
        m_data->tables.erase(std::string(name));
    } else {
        m_data->tables.insert_or_assign(std::string(name), table->m_data);
    }
}

std::optional<SharedTable>
SharedTableRegistry::get_shared_table(std::string_view name) const {
    validate_name(name, "SharedTableRegistry name");
    std::lock_guard lock(m_data->mutex);
    const auto iterator = m_data->tables.find(std::string(name));
    if (iterator == m_data->tables.end()) {
        return std::nullopt;
    }
    return SharedTable(iterator->second);
}

void SharedTableRegistry::install(State &state,
                                  std::string_view sharedTableGlobal,
                                  std::string_view registryGlobal) const {
    validate_name(sharedTableGlobal, "SharedTable global name");
    validate_name(registryGlobal, "SharedTableRegistry global name");
    if (sharedTableGlobal == registryGlobal) {
        throw std::invalid_argument(
            "SharedTable global names must be distinct");
    }

    lua_State *lua = state.checked_state();
    StackRestore restore(lua);
    for (const std::string_view globalName :
         {sharedTableGlobal, registryGlobal}) {
        lua_getglobal(lua, std::string(globalName).c_str());
        const bool exists = !lua_isnil(lua, -1);
        lua_pop(lua, 1);
        if (exists) {
            throw std::invalid_argument("global '" + std::string(globalName) +
                                        "' is already registered");
        }
    }

    install_table_metatable(lua);
    auto bindings = std::make_shared<detail::SharedTableBindings>();
    bindings->registry = m_data;
    state.m_lifetime->bindingRegistrations.push_back(bindings);
    state.m_lifetime->sharedTableRegistry = m_data;

    lua_createtable(lua, 0, 1);
    lua_pushcclosure(lua, shared_table_new, "SharedTable.new", 0);
    lua_setfield(lua, -2, "new");
    lua_setreadonly(lua, -1, true);
    lua_setglobal(lua, std::string(sharedTableGlobal).c_str());

    lua_createtable(lua, 0, 2);
    lua_pushlightuserdata(lua, bindings.get());
    lua_pushcclosure(lua, registry_set, "SharedTableRegistry.SetSharedTable",
                     1);
    lua_setfield(lua, -2, "SetSharedTable");
    lua_pushlightuserdata(lua, bindings.get());
    lua_pushcclosure(lua, registry_get, "SharedTableRegistry.GetSharedTable",
                     1);
    lua_setfield(lua, -2, "GetSharedTable");
    lua_setreadonly(lua, -1, true);
    lua_setglobal(lua, std::string(registryGlobal).c_str());
}

} // namespace lunaris
