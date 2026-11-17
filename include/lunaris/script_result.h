#pragma once

#include "lunaris/detail/state_context.h"
#include "lunaris/state.h"
#include "lunaris/type.h"

#include <memory>
#include <vector>

namespace lunaris {
/// @brief Represents the result of executing a script in a Luau state. The
/// result is a collection of values that were returned by the script. The
/// ScriptResult object does not own the underlying Luau state, which is managed
/// by the State object. The ScriptResult object is move-only, meaning it can be
/// moved but not copied. This is because the ScriptResult object holds a shared
/// pointer to the StateContext, which manages the lifetime of the underlying
/// lua_State. Moving a ScriptResult object transfers ownership of the shared
/// pointer, while copying would create multiple owners of the same lua_State,
/// which is not allowed.
class ScriptResult {
  public:
    /// @brief Destroys the ScriptResult object. The underlying Luau state is
    /// not closed, as it is managed by the State object.
    ~ScriptResult() = default;

    /// @brief The move constructor for ScriptResult.
    ScriptResult(ScriptResult &&other) noexcept = default;

    /// @brief The move assignment operator for ScriptResult.
    ScriptResult &operator=(ScriptResult &&other) noexcept = default;

    /// @brief the copy constructor for ScriptResult.
    ScriptResult(const ScriptResult &other) = delete;

    /// @brief The copy assignment operator for ScriptResult.
    ScriptResult &operator=(const ScriptResult &other) = delete;

    /// @brief the number of values in the result.
    /// @return The number of values in the result.
    std::uint32_t size() const noexcept;

    /// @brief returns the type of the value at the given index in the result.
    /// The index is 1-based, meaning the first value is at index 1. If the
    /// index is out of bounds, the function returns Type::unknown. The index is
    /// 1-based, meaning the first value is at index 1. If the index is out of
    /// bounds, the function returns Type::unknown.
    /// @param index The 1-based index of the value in the result.
    /// @return The type of the value at the given index, or Type::unknown if
    /// the index is out of bounds.
    Type type(std::uint32_t index) const noexcept;

  private:
    friend class ::lunaris::State;

    /// @brief Constructs a ScriptResult object with the given state context and
    /// types. This constructor is private and can only be called by the State
    /// class, which is a friend of ScriptResult. The state context is a shared
    /// pointer to the StateContext object that manages the lifetime of the
    /// underlying lua_State. The types vector contains the types of the values
    /// in the result, in order.
    ScriptResult(std::shared_ptr<detail::StateContext> state_context,
                 std::vector<Type> &types) noexcept
        : m_state_context(std::move(state_context)),
          m_types(std::move(types)) {};

    /// @brief A shared pointer to the StateContext object that manages the
    /// lifetime of the underlying lua_State. The shared pointer ensures that
    /// the StateContext object is not destroyed while the ScriptResult object
    /// is still alive. The StateContext object is created by the State class
    /// and is shared among all ScriptResult objects that are created from the
    /// same State object.
    std::shared_ptr<detail::StateContext> m_state_context;

    /// @brief A vector of types that represents the types of the values in the
    /// result, in order. The types are stored as Type enum values, which are
    /// defined in the Type enum.
    std::vector<Type> m_types;
};
} // namespace lunaris
