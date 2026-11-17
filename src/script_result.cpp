#include "lunaris/script_result.h"

namespace lunaris {
std::uint32_t ScriptResult::size() const noexcept {
    return static_cast<std::uint32_t>(m_types.size());
}

Type ScriptResult::type(std::uint32_t index) const noexcept {
    if (index == 0 || index > m_types.size()) {
        return Type::unknown;
    }
    return m_types[index - 1];
}
} // namespace lunaris
