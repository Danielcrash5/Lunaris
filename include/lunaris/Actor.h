#pragma once

#include "lunaris/Future.h"
#include "lunaris/MessageQueue.h"
#include "lunaris/Result.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace lunaris {

class State;

/// Host-side, scheduler-neutral actor mailbox and message dispatcher.
/// The host must invoke dispatch methods on the Actor's chosen execution
/// context; Actor does not own or access a Luau State.
class Actor {
  public:
    using Handler = std::function<Result<void>(const Message &)>;
    using RequestHandler = std::function<Result<Value>(const Message &)>;

    Actor() = default;
    Actor(const Actor &) = delete;
    Actor &operator=(const Actor &) = delete;

    /// Installs a shared Actor handle as a Luau global. Luau can send scalar
    /// messages to this Actor; the host remains responsible for dispatch.
    /// The global name must be unused in the target State.
    static void install(State &state, std::string_view globalName,
                        std::shared_ptr<Actor> actor);

    /// Adds a handler for a topic. Duplicate topic bindings are rejected.
    void bind(std::string_view topic, Handler handler);

    /// Adds a request handler for a topic. Its result completes each request.
    /// Duplicate topic bindings are rejected.
    void bind_request(std::string_view topic, RequestHandler handler);

    /// Removes the handler for a topic.
    [[nodiscard]] bool unbind(std::string_view topic);

    /// Enqueues a message for later dispatch.
    [[nodiscard]] bool send(std::string_view topic, Value payload);

    /// Enqueues a request whose Future is completed by a request handler.
    /// A closed Actor returns an already-failed Future.
    [[nodiscard]] Future<Value> request(std::string_view topic, Value payload);

    /// Dispatches at most one queued message. Returns false if the queue is
    /// empty. Handler errors are returned after consuming that message.
    [[nodiscard]] Result<bool> dispatch_one();

    /// Dispatches queued messages in FIFO order until empty or a handler
    /// returns an error. Concurrent or reentrant dispatch attempts return an
    /// error; the host should serialize dispatch on the Actor's context.
    [[nodiscard]] Result<std::size_t> dispatch_pending();

    /// Closes the mailbox, preserving already queued messages for dispatch.
    void close();

    [[nodiscard]] bool is_closed() const;
    [[nodiscard]] std::size_t pending() const;

  private:
    [[nodiscard]] Result<bool> dispatch_one_locked();
    void validate_topic(std::string_view topic) const;

    MessageQueue m_messages;
    std::mutex m_dispatchMutex;
    std::mutex m_handlersMutex;
    std::unordered_map<std::string, std::variant<Handler, RequestHandler>>
        m_handlers;
};

} // namespace lunaris
