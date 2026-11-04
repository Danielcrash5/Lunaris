#pragma once

#include "lunaris/Value.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace lunaris {

class State;
namespace detail {
struct SharedTableData;
struct SharedTableRegistryData;
struct SharedTableBindings;
} // namespace detail

/// Thread-safe handle to data shared between installed Luau States.
class SharedTable {
  public:
    SharedTable() = default;

    /// Reads a scalar value; nested tables must be read with get_table().
    [[nodiscard]] std::optional<Value> get(std::string_view key) const;

    /// Reads a nested SharedTable, if the key exists and contains one.
    [[nodiscard]] std::optional<SharedTable>
    get_table(std::string_view key) const;

    /// Stores a scalar. A nil value removes the key.
    void set(std::string_view key, const Value &value) const;

    /// Stores a nested SharedTable.
    void set(std::string_view key, const SharedTable &value) const;

    /// Removes a key and returns whether it existed.
    [[nodiscard]] bool remove(std::string_view key) const;

    /// Returns the number of entries.
    [[nodiscard]] std::size_t size() const;

    /// Returns whether this handle refers to a table.
    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    friend class SharedTableRegistry;
    friend struct detail::SharedTableRegistryData;
    friend struct detail::SharedTableData;
    friend struct detail::SharedTableBindings;
    explicit SharedTable(std::shared_ptr<detail::SharedTableData> data);

    std::shared_ptr<detail::SharedTableData> m_data;
};

/// Explicit registry shared by any States to which it is installed.
class SharedTableRegistry {
  public:
    SharedTableRegistry();

    /// Creates a new empty SharedTable.
    [[nodiscard]] SharedTable create_table() const;

    /// Publishes or removes a named SharedTable.
    /// Passing nullopt removes the name.
    void set_shared_table(std::string_view name,
                          std::optional<SharedTable> table);

    /// Retrieves a named SharedTable.
    [[nodiscard]] std::optional<SharedTable>
    get_shared_table(std::string_view name) const;

    /// Installs SharedTable and SharedTableRegistry globals into a State.
    /// The registry and its stored tables remain shared across installed
    /// States. Both global names must be unused in the target State.
    void install(State &state,
                 std::string_view sharedTableGlobal = "SharedTable",
                 std::string_view registryGlobal = "SharedTableRegistry") const;

  private:
    std::shared_ptr<detail::SharedTableRegistryData> m_data;
};

} // namespace lunaris
