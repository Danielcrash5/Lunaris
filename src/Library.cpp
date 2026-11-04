#include "lunaris/Library.h"

#include <lua.h>

#include <stdexcept>
#include <utility>

namespace lunaris {

namespace {

std::shared_ptr<detail::StateLifetime>
checked_lifetime(const std::weak_ptr<detail::StateLifetime> &weakLifetime,
                 int reference) {
    auto lifetime = weakLifetime.lock();
    if (lifetime == nullptr || lifetime->state == nullptr || reference < 0) {
        throw std::logic_error("library State is no longer available");
    }
    return lifetime;
}

} // namespace

Library::Library(std::weak_ptr<detail::StateLifetime> lifetime, int reference,
                 std::string name)
    : m_lifetime(std::move(lifetime)), m_reference(reference),
      m_name(std::move(name)) {
}

Library::Library(Library &&other) noexcept
    : m_lifetime(std::move(other.m_lifetime)),
      m_reference(std::exchange(other.m_reference, -1)),
      m_name(std::move(other.m_name)) {
}

Library &Library::operator=(Library &&other) noexcept {
    if (this != &other) {
        release();
        m_lifetime = std::move(other.m_lifetime);
        m_reference = std::exchange(other.m_reference, -1);
        m_name = std::move(other.m_name);
    }
    return *this;
}

Library::~Library() {
    release();
}

std::shared_ptr<detail::StateLifetime> Library::checked_lifetime() const {
    return lunaris::checked_lifetime(m_lifetime, m_reference);
}

void Library::validate_name(std::string_view name) {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "library member name must be non-empty and contain no null byte");
    }
}

std::string Library::qualified_type_name(std::string_view name) const {
    validate_name(name);
    return m_name + "." + std::string(name);
}

void Library::set_constant(std::string_view name, const Value &value) {
    validate_name(name);
    if (std::holds_alternative<std::monostate>(value)) {
        throw std::invalid_argument("library constants cannot be nil");
    }
    with_table([&](lua_State *state, int table) {
        detail::ensure_library_field_available(state, table, name);
        std::visit(
            [state](const auto &item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<T, std::monostate>) {
                    lua_pushnil(state);
                } else {
                    detail::push_value(state, item);
                }
            },
            value);
        lua_setreadonly(state, table, false);
        lua_setfield(state, table, std::string(name).c_str());
        lua_setreadonly(state, table, true);
    });
}

void Library::register_module(std::string_view moduleName,
                              LibraryVisibility visibility) {
    if (moduleName.empty() || moduleName.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "library module name must be non-empty and contain no null byte");
    }
    if (visibility != LibraryVisibility::GlobalAndModule &&
        visibility != LibraryVisibility::ModuleOnly) {
        throw std::invalid_argument("invalid library visibility");
    }

    auto lifetime = checked_lifetime();
    detail::register_library_module(
        lifetime->state, lifetime, moduleName, m_reference, m_name,
        visibility == LibraryVisibility::ModuleOnly);
}

void Library::release() noexcept {
    if (m_reference < 0) {
        return;
    }
    auto lifetime = m_lifetime.lock();
    if (lifetime != nullptr && lifetime->state != nullptr) {
        lua_unref(lifetime->state, m_reference);
    }
    m_reference = -1;
}

} // namespace lunaris
