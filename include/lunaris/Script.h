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

/// Policy for handling a script's private environment during reload.
enum class ReloadMode { Reset, PreserveEnvironment };

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

    /// Reads a scalar from the script environment, including inherited State
    /// globals; missing names return nil.
    /// @return The value, or a Type error for reference values or invalid
    /// names.
    [[nodiscard]] Result<Value> get_global(std::string_view name) const;

    /// Writes a scalar to this script's private environment.
    /// Setting nil removes the script-local value and exposes any inherited
    /// State global with the same name.
    /// @return Success, or a Type error if the name contains a null byte.
    [[nodiscard]] Result<void> set_global(std::string_view name,
                                          const Value &value);

    /// Compiles and executes replacement source, committing the new chunk only
    /// after successful execution.
    /// Reset starts with a fresh environment; PreserveEnvironment reuses the
    /// existing one. Compile/runtime/type failures retain the previous chunk.
    /// Runtime side effects are not rolled back.
    [[nodiscard]] Result<std::vector<Value>>
    reload(std::string_view source, ReloadMode mode = ReloadMode::Reset);

  private:
    friend class State;

    Script(std::weak_ptr<detail::StateLifetime> lifetime, int reference,
           int environmentReference, std::string source);
    void release() noexcept;

    std::weak_ptr<detail::StateLifetime> m_lifetime;
    int m_reference = -1;
    int m_environmentReference = -1;
    std::string m_source;
};

} // namespace lunaris
