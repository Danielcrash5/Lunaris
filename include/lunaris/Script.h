#pragma once

#include "lunaris/Error.h"
#include "lunaris/Result.h"
#include "lunaris/Value.h"
#include "lunaris/detail/StateLifetime.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lunaris {

class State;

/// Compiled source chunk owned by a State.
class Script {
  public:
    Script(const Script &) = delete;
    Script &operator=(const Script &) = delete;

    /// Releases the compiled function reference if its State is still alive.
    ~Script();

    /// Transfers the compiled script handle.
    Script(Script &&other) noexcept;

    /// Releases this handle and transfers another compiled script.
    Script &operator=(Script &&other) noexcept;

    /// Executes the compiled chunk and returns all values it returns.
    /// Runtime failures are returned as structured errors. The State stack is
    /// restored to its previous height on success and failure.
    /// Reference return values are reported as Type errors because Value
    /// currently supports scalar values only.
    [[nodiscard]] Result<std::vector<Value>> execute();

  private:
    friend class State;

    Script(std::weak_ptr<detail::StateLifetime> lifetime, int reference,
           std::string source);
    void release() noexcept;

    std::weak_ptr<detail::StateLifetime> m_lifetime;
    int m_reference = -1;
    std::string m_source;
};

} // namespace lunaris
