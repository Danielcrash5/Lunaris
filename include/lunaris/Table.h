#pragma once

#include "lunaris/Value.h"
#include "lunaris/detail/StateLifetime.h"

#include <memory>
#include <string_view>

namespace lunaris {

class State;

/// Move-only handle to a Luau table kept alive by the registry.
class Table {
  public:
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;

    /// Releases the registry reference if its State is still alive.
    ~Table();

    /// Transfers the table handle.
    Table(Table &&other) noexcept;

    /// Releases this handle and transfers another table handle.
    Table &operator=(Table &&other) noexcept;

    /// Reads a string field as a scalar value; absent fields read as nil.
    /// Raw field access is used, so metamethods are not invoked.
    [[nodiscard]] Value get(std::string_view key) const;

    /// Sets a string field to a scalar value. Setting nil removes the field.
    /// Raw field access is used, so metamethods are not invoked.
    void set(std::string_view key, const Value &value);

  private:
    friend class State;

    Table(std::weak_ptr<detail::StateLifetime> lifetime, int reference);
    void release() noexcept;

    std::weak_ptr<detail::StateLifetime> m_lifetime;
    int m_reference = -1;
};

} // namespace lunaris
