#pragma once

#include "lunaris/type.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lunaris {

class State;

namespace detail {
class StateContext;
} // namespace detail

/// @brief The values a script returned.
///
/// A script can return any number of values (`return 1, "a"`). A ScriptResult
/// keeps them alive in the Luau state until it is destroyed, so that they can
/// be inspected afterwards. Indices start at 0.
///
/// A ScriptResult shares the context of the State it comes from. It becomes
/// invalid when that State is closed: size() still reports the number of
/// values, but type() and get<T>() throw. Destroying it afterwards is safe.
///
/// A ScriptResult cannot be copied, only moved. A moved-from object is empty
/// and may only be destroyed or assigned to.
class ScriptResult {
  public:
    /// @brief Releases the values this object holds. Does nothing if the State
    /// is already closed. Never throws.
    ~ScriptResult();

    /// @brief Moves the values of another ScriptResult into this one. The other
    /// object is empty afterwards.
    /// @param other The ScriptResult to move from.
    ScriptResult(ScriptResult &&other) noexcept = default;

    /// @brief Releases the values this object holds and takes over those of
    /// another ScriptResult. The other object is empty afterwards. Assigning an
    /// object to itself has no effect.
    /// @param other The ScriptResult to move from.
    /// @return A reference to this ScriptResult.
    ScriptResult &operator=(ScriptResult &&other) noexcept;

    /// @brief Deleted copy constructor. A ScriptResult cannot be copied.
    ScriptResult(const ScriptResult &other) = delete;

    /// @brief Deleted copy assignment operator. A ScriptResult cannot be
    /// copied.
    /// @return Not applicable, the operator is deleted.
    ScriptResult &operator=(const ScriptResult &other) = delete;

    /// @brief Gets the number of values the script returned.
    /// @return The number of values. 0 if the script returned nothing.
    [[nodiscard]] std::uint32_t size() const noexcept;

    /// @brief Gets the type of a returned value.
    /// @param index The index of the value, starting at 0.
    /// @return The type of the value at the given index.
    /// @throws std::out_of_range if the index is not smaller than size().
    /// @throws Error with ErrorKind::state if the State is closed.
    [[nodiscard]] Type type(std::uint32_t index) const;

    /// @brief Gets the value at the given index, converted to the requested
    /// type.
    /// @tparam T The type to convert the value to. Must be bool, double
    /// or std::string.
    /// @param index The index of the value, starting at 0.
    /// @return The value at the given index, converted to the requested type.
    /// @throws std::out_of_range if the index is not smaller than size().
    /// @throws Error with ErrorKind::state if the State is closed.
    /// @throws Error with ErrorKind::type if the value cannot be converted to
    /// the requested type.
    /// @throws std::bad_alloc if returning a string runs out of memory.
    template <typename T>
        requires(std::same_as<T, bool> || std::same_as<T, double> ||
                 std::same_as<T, std::string>)
    [[nodiscard]] T get(std::uint32_t index) const {
        if constexpr (std::same_as<T, bool>) {
            return getBool(index);
        } else if constexpr (std::same_as<T, double>) {
            return getDouble(index);
        } else {
            return getString(index);
        }
    }

  private:
    friend class ::lunaris::State;

    [[nodiscard]] bool getBool(std::uint32_t index) const;
    [[nodiscard]] double getDouble(std::uint32_t index) const;
    [[nodiscard]] std::string getString(std::uint32_t index) const;

    /// @brief Creates a ScriptResult. Only a State creates them.
    /// @param stateContext The context of the State the values live in.
    /// @param references Registry references to the values, in order.
    ScriptResult(std::shared_ptr<detail::StateContext> stateContext,
                 std::vector<int> references) noexcept
        : m_stateContext(std::move(stateContext)),
          m_references(std::move(references)) {
    }

    /// @brief Releases all references, if the State is still open.
    void release() noexcept;

    /// @brief The context of the State the values live in.
    std::shared_ptr<detail::StateContext> m_stateContext;

    /// @brief Registry references to the returned values, in order.
    std::vector<int> m_references;
};

} // namespace lunaris
