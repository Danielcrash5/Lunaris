#include <lunaris/error.h>
#include <stdexcept>
#include <string>

namespace lunaris {
Error::Error(const ErrorDetails &details)
    : std::runtime_error::runtime_error(details.message), m_details(details) {
}

const ErrorDetails &Error::details() const noexcept {
    return m_details;
}

ErrorKind Error::kind() const noexcept {
    return m_details.kind;
}

const std::string &Error::chunkName() const noexcept {
    return m_details.chunkName;
}

std::optional<std::uint32_t> Error::lineNumber() const noexcept {
    return m_details.lineNumber;
}

const std::string &Error::traceback() const noexcept {
    return m_details.traceback;
}
} // namespace lunaris
