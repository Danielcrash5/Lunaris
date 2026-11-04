#include "lunaris/Error.h"

#include <utility>

namespace lunaris {

Error::Error(ErrorType type, std::string message, std::string source, int line,
             int column)
    : m_type(type), m_message(std::move(message)), m_source(std::move(source)),
      m_line(line), m_column(column) {
}

ErrorType Error::type() const noexcept {
    return m_type;
}

std::string_view Error::message() const noexcept {
    return m_message;
}

std::string_view Error::source() const noexcept {
    return m_source;
}

int Error::line() const noexcept {
    return m_line;
}

int Error::column() const noexcept {
    return m_column;
}

} // namespace lunaris
