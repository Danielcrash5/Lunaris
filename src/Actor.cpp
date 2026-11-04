#include "lunaris/Actor.h"

#include "lunaris/Error.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace lunaris {

void Actor::validate_topic(std::string_view topic) const {
    if (topic.empty() || topic.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "actor topic must be non-empty and contain no null byte");
    }
}

void Actor::bind(std::string_view topic, Handler handler) {
    validate_topic(topic);
    if (!handler) {
        throw std::invalid_argument("actor handler cannot be empty");
    }
    std::lock_guard lock(m_handlersMutex);
    const auto [iterator, inserted] =
        m_handlers.emplace(std::string(topic), std::move(handler));
    if (!inserted) {
        throw std::invalid_argument("actor topic already has a handler");
    }
}

bool Actor::unbind(std::string_view topic) {
    validate_topic(topic);
    std::lock_guard lock(m_handlersMutex);
    return m_handlers.erase(std::string(topic)) != 0;
}

bool Actor::send(std::string_view topic, Value payload) {
    return m_messages.send(topic, std::move(payload));
}

Result<bool> Actor::dispatch_one_locked() {
    auto message = m_messages.try_receive();
    if (!message) {
        return Result<bool>::success(false);
    }

    Handler handler;
    {
        std::lock_guard lock(m_handlersMutex);
        const auto iterator = m_handlers.find(message->topic);
        if (iterator == m_handlers.end()) {
            return Result<bool>::failure(
                Error(ErrorType::Runtime, "actor has no handler for topic '" +
                                              message->topic + "'"));
        }
        handler = iterator->second;
    }

    try {
        auto result = handler(*message);
        if (!result) {
            return Result<bool>::failure(result.error());
        }
    } catch (const std::exception &exception) {
        return Result<bool>::failure(
            Error(ErrorType::Runtime, "actor handler threw an exception: " +
                                          std::string(exception.what())));
    } catch (...) {
        return Result<bool>::failure(
            Error(ErrorType::Runtime,
                  "actor handler threw a non-standard exception"));
    }
    return Result<bool>::success(true);
}

Result<bool> Actor::dispatch_one() {
    std::unique_lock lock(m_dispatchMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return Result<bool>::failure(
            Error(ErrorType::Runtime, "actor dispatch is already in progress"));
    }
    return dispatch_one_locked();
}

Result<std::size_t> Actor::dispatch_pending() {
    std::unique_lock lock(m_dispatchMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return Result<std::size_t>::failure(
            Error(ErrorType::Runtime, "actor dispatch is already in progress"));
    }
    std::size_t dispatched = 0;
    while (true) {
        auto result = dispatch_one_locked();
        if (!result) {
            return Result<std::size_t>::failure(result.error());
        }
        if (!result.value()) {
            return Result<std::size_t>::success(dispatched);
        }
        ++dispatched;
    }
}

void Actor::close() {
    m_messages.close();
}

bool Actor::is_closed() const {
    return m_messages.is_closed();
}

std::size_t Actor::pending() const {
    return m_messages.size();
}

} // namespace lunaris
