#pragma once

#include "lunaris/error.h"
#include "lunaris/script_result.h"

#include <lua.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

namespace lunaris {

namespace detail {
class StateContext;
} // namespace detail

/// @brief The standard libraries of Luau that can be opened in a State.
///
/// Passed to State::openLibraries(). The libraries `integer` and `class`
/// that exist in some Luau versions are experimental and not included.
enum class Library : std::uint8_t {
    base,      ///< Global functions such as `print`, `type` and `pairs`.
    coroutine, ///< The `coroutine` table.
    table,     ///< The `table` table.
    os,        ///< The `os` table (clock, date and time functions only).
    string,    ///< The `string` table.
    math,      ///< The `math` table.
    debug,     ///< The `debug` table.
    utf8,      ///< The `utf8` table.
    bit32,     ///< The `bit32` table.
    buffer,    ///< The `buffer` table.
    vector,    ///< The `vector` table.
};
/// Owns a Luau state and closes it when destroyed.
class State {
  public:
    /// @brief Creates a new Luau state. Throws std::bad_alloc if the state
    /// could not be created.
    State();
    /// @brief Destroys the Luau state.
    ~State();

    /// @brief Move constructor. Transfers ownership of the underlying lua_State
    /// pointer from another State object to this one. The other State object
    /// will be left in a moved-from state, and its luaState() method will
    /// return nullptr.
    /// @param other The State object to move from.
    State(State &&other) noexcept = default;

    /// @brief Move assignment operator. Closes the Luau state this object owned
    /// until now and transfers ownership of the underlying lua_State pointer
    /// from another State object to this one. The other State object will be
    /// left in a moved-from state, and its luaState() method will return
    /// nullptr. Assigning an object to itself has no effect.
    /// @param other The State object to move from.
    /// @return A reference to this State.
    State &operator=(State &&other) noexcept;

    /// @brief Copy constructor: deleted to prevent copying of the State object,
    /// as it owns a unique resource (the lua_State pointer).
    State(const State &) = delete;

    /// @brief Copy assignment operator: deleted to prevent copying of the State
    /// object, as it owns a unique resource (the lua_State pointer).
    State &operator=(const State &) = delete;

    /// @brief returns the underlying lua_State pointer. The pointer is valid as
    /// long as the State object is alive.
    /// @return the underlying lua_State pointer. Is nullptr if the State object
    /// is in a moved-from state.
    [[nodiscard]] lua_State *luaState() const noexcept;

    /// @brief Returns the underlying state context.
    /// @return The underlying state context.
    [[nodiscard]] std::shared_ptr<detail::StateContext>
    context() const noexcept;

    /// @brief Opens the given standard libraries. Libraries that are not
    /// opened are not available to scripts.
    ///
    /// The order of the arguments does not matter, libraries are always opened
    /// in the same order. A library that is listed twice is opened once.
    /// Calling this function again registers the libraries anew, which discards
    /// changes scripts made to them. Open libraries before running scripts.
    ///
    /// The State must not be in a moved-from state.
    ///
    /// @param libs One or more libraries. Every argument must be a Library.
    /// @throws std::invalid_argument if an argument is not a valid Library
    /// value. Nothing is opened in that case.
    /// @throws std::bad_alloc if a library could not be opened because of
    /// missing memory. Libraries opened before stay open (basic guarantee).
    template <typename... Libs>
        requires(sizeof...(Libs) > 0 && (std::same_as<Libs, Library> && ...))
    void openLibraries(Libs... libs) {
        const std::array<Library, sizeof...(Libs)> selected{libs...};
        openLibrariesImpl(selected);
    }

    /// @brief Opens all standard libraries of the Library enumeration.
    ///
    /// The same rules as for openLibraries() apply.
    /// @throws std::bad_alloc if a library could not be opened because of
    /// missing memory. Libraries opened before stay open (basic guarantee).
    void openAllLibraries();

    /// @brief Runs a script and returns the values it returned.
    ///
    /// The source is compiled and run in the global scope. The chunk name is
    /// the name that appears in error messages and tracebacks, Lunaris puts a
    /// `=` in front of it so that it is shown as given.
    ///
    /// The State must not be in a moved-from state. The chunk name must not
    /// contain a `\0`.
    ///
    /// @param source The Luau source code.
    /// @param chunkName The name of the chunk, shown in error messages.
    /// @return The values the script returned, in order.
    /// @throws Error with ErrorKind::compile if the source does not compile.
    /// @throws Error with ErrorKind::runtime if the script raises an error.
    /// @throws Error with ErrorKind::memory if the State runs out of memory.
    ScriptResult script(std::string_view source,
                        std::string_view chunkName = "script");

    /// @brief Runs a script like script() but reports script errors as a
    /// value instead of throwing them.
    ///
    /// The same preconditions as for script() apply.
    ///
    /// @param source The Luau source code.
    /// @param chunkName The name of the chunk, shown in error messages.
    /// @return The values the script returned, or the Error that describes why
    /// the script failed.
    [[nodiscard]] std::expected<ScriptResult, Error>
    safeScript(std::string_view source, std::string_view chunkName = "script");

  private:
    /// @brief Opens the selected libraries. Does the work for openLibraries(),
    /// so that the logic lives in the source file and not in the header.
    /// @param libraries The libraries to open, may contain duplicates.
    void openLibrariesImpl(std::span<const Library> libraries);

    std::shared_ptr<detail::StateContext> m_context;
};
} // namespace lunaris
