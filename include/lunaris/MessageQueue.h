#pragma once

#include "lunaris/Future.h"
#include "lunaris/Value.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace lunaris {

/// One scalar-payload message delivered through a MessageQueue.
struct Message {
    std::string topic;
    Value payload;
    std::shared_ptr<Promise<Value>> response;
};

/// Thread-safe, unbounded FIFO mailbox. It does not schedule handlers.
class MessageQueue {
  public:
    MessageQueue() = default;
    MessageQueue(const MessageQueue &) = delete;
    MessageQueue &operator=(const MessageQueue &) = delete;

    /// Enqueues a message, or returns false if the queue is closed.
    /// Topics must be non-empty and contain no embedded null bytes.
    [[nodiscard]] bool
    send(std::string_view topic, Value payload,
         std::shared_ptr<Promise<Value>> response = nullptr) {
        if (topic.empty() || topic.find('\0') != std::string_view::npos) {
            throw std::invalid_argument(
                "message topic must be non-empty and contain no null byte");
        }

        {
            std::lock_guard lock(m_mutex);
            if (m_closed) {
                return false;
            }
            m_messages.push_back(Message{std::string(topic), std::move(payload),
                                         std::move(response)});
        }
        m_condition.notify_one();
        return true;
    }

    /// Returns the next queued message without waiting, if one is available.
    [[nodiscard]] std::optional<Message> try_receive() {
        std::lock_guard lock(m_mutex);
        if (m_messages.empty()) {
            return std::nullopt;
        }
        return take_front();
    }

    /// Waits until a message arrives or the closed queue has been drained.
    /// Returns nullopt only when closed and empty.
    [[nodiscard]] std::optional<Message> receive() {
        std::unique_lock lock(m_mutex);
        m_condition.wait(lock,
                         [this] { return m_closed || !m_messages.empty(); });
        if (m_messages.empty()) {
            return std::nullopt;
        }
        return take_front();
    }

    /// Waits up to timeout for a message; nullopt indicates timeout or a
    /// closed and drained queue. Use is_closed() to distinguish those cases.
    template <typename Rep, typename Period>
    [[nodiscard]] std::optional<Message>
    receive_for(const std::chrono::duration<Rep, Period> &timeout) {
        std::unique_lock lock(m_mutex);
        if (!m_condition.wait_for(lock, timeout, [this] {
                return m_closed || !m_messages.empty();
            })) {
            return std::nullopt;
        }
        if (m_messages.empty()) {
            return std::nullopt;
        }
        return take_front();
    }

    /// Rejects future sends and wakes all waiting receivers. Queued messages
    /// remain available to receive.
    void close() {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_condition.notify_all();
    }

    [[nodiscard]] bool is_closed() const {
        std::lock_guard lock(m_mutex);
        return m_closed;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(m_mutex);
        return m_messages.size();
    }

  private:
    [[nodiscard]] Message take_front() {
        Message message = std::move(m_messages.front());
        m_messages.pop_front();
        return message;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<Message> m_messages;
    bool m_closed = false;
};

} // namespace lunaris
