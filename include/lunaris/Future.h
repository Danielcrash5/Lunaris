#pragma once

#include "lunaris/Error.h"
#include "lunaris/Result.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace lunaris {

enum class FutureStatus { Pending, Succeeded, Failed, Cancelled };

namespace detail {

template <typename T> struct FutureState {
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<Result<T>> result;
    bool broken = false;
    bool futureRetrieved = false;
    bool resultConsumed = false;
    std::vector<std::weak_ptr<std::atomic_bool>> readinessSignals;
};

template <typename T> void mark_ready(FutureState<T> &state) noexcept {
    for (auto iterator = state.readinessSignals.begin();
         iterator != state.readinessSignals.end();) {
        if (auto signal = iterator->lock()) {
            signal->store(true, std::memory_order_release);
            ++iterator;
        } else {
            iterator = state.readinessSignals.erase(iterator);
        }
    }
}

} // namespace detail

template <typename T> class Promise;

/// A single-consumer result that can be completed by a host scheduler.
template <typename T> class Future {
  public:
    Future() = default;
    Future(const Future &) = delete;
    Future &operator=(const Future &) = delete;
    Future(Future &&) noexcept = default;
    Future &operator=(Future &&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return m_state != nullptr;
    }

    [[nodiscard]] FutureStatus status() const {
        const auto state = checked_state();
        std::lock_guard lock(state->mutex);
        if (state->broken) {
            return FutureStatus::Failed;
        }
        if (!state->result) {
            return FutureStatus::Pending;
        }
        if (state->result->has_value()) {
            return FutureStatus::Succeeded;
        }
        return state->result->error().type() == ErrorType::Cancelled
                   ? FutureStatus::Cancelled
                   : FutureStatus::Failed;
    }

    [[nodiscard]] bool is_ready() const {
        return status() != FutureStatus::Pending;
    }

    void wait() const {
        const auto state = checked_state();
        std::unique_lock lock(state->mutex);
        state->condition.wait(
            lock, [&] { return state->result.has_value() || state->broken; });
    }

    template <typename Rep, typename Period>
    [[nodiscard]] bool
    wait_for(const std::chrono::duration<Rep, Period> &timeout) const {
        const auto state = checked_state();
        std::unique_lock lock(state->mutex);
        return state->condition.wait_for(lock, timeout, [&] {
            return state->result.has_value() || state->broken;
        });
    }

    /// Retrieves the result once; errors, including cancellation, are returned.
    [[nodiscard]] Result<T> get() {
        wait();
        const auto state = checked_state();
        std::lock_guard lock(state->mutex);
        if (state->resultConsumed) {
            throw std::logic_error("Future result was already consumed");
        }
        state->resultConsumed = true;
        if (state->broken) {
            return Result<T>::failure(
                Error(ErrorType::Future, "Promise was abandoned"));
        }
        return std::move(*state->result);
    }

    /// Completes the Future as cancelled if no result has won the race.
    /// This does not interrupt work already running in the host scheduler.
    [[nodiscard]] bool cancel() {
        const auto state = checked_state();
        {
            std::lock_guard lock(state->mutex);
            if (state->result || state->broken) {
                return false;
            }
            state->result.emplace(Result<T>::failure(
                Error(ErrorType::Cancelled, "Future was cancelled")));
            detail::mark_ready(*state);
        }
        state->condition.notify_all();
        return true;
    }

    /// Registers a thread-safe readiness signal for host-driven polling.
    /// The signal is set when the Future resolves, fails, is cancelled, or
    /// its Promise is abandoned.
    void observe_ready(const std::shared_ptr<std::atomic_bool> &signal) const {
        if (signal == nullptr) {
            throw std::invalid_argument("Future readiness signal is null");
        }
        const auto state = checked_state();
        std::lock_guard lock(state->mutex);
        if (state->result || state->broken) {
            signal->store(true, std::memory_order_release);
            return;
        }
        state->readinessSignals.emplace_back(signal);
    }

  private:
    friend class Promise<T>;

    explicit Future(std::shared_ptr<detail::FutureState<T>> state)
        : m_state(std::move(state)) {
    }

    [[nodiscard]] std::shared_ptr<detail::FutureState<T>>
    checked_state() const {
        if (!m_state) {
            throw std::logic_error("operation on an invalid Future");
        }
        return m_state;
    }

    std::shared_ptr<detail::FutureState<T>> m_state;
};

/// Completes a Future once with a value, error, or cancellation.
template <typename T> class Promise {
    static_assert(!std::is_void_v<T>, "Use Promise<void> for void results");

  public:
    Promise() : m_state(std::make_shared<detail::FutureState<T>>()) {
    }

    Promise(const Promise &) = delete;
    Promise &operator=(const Promise &) = delete;

    Promise(Promise &&other) noexcept : m_state(std::move(other.m_state)) {
    }

    Promise &operator=(Promise &&other) noexcept {
        if (this != &other) {
            abandon();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    ~Promise() {
        abandon();
    }

    [[nodiscard]] Future<T> get_future() {
        const auto state = checked_state();
        std::lock_guard lock(state->mutex);
        if (state->futureRetrieved) {
            throw std::logic_error("Promise Future was already retrieved");
        }
        state->futureRetrieved = true;
        return Future<T>(state);
    }

    [[nodiscard]] bool set_value(T value) {
        return complete(Result<T>::success(std::move(value)));
    }

    [[nodiscard]] bool set_error(Error error) {
        return complete(Result<T>::failure(std::move(error)));
    }

    [[nodiscard]] bool cancel() {
        return complete(Result<T>::failure(
            Error(ErrorType::Cancelled, "Future was cancelled")));
    }

    [[nodiscard]] bool set_result(Result<T> result) {
        return complete(std::move(result));
    }

  private:
    [[nodiscard]] std::shared_ptr<detail::FutureState<T>>
    checked_state() const {
        if (!m_state) {
            throw std::logic_error("operation on an invalid Promise");
        }
        return m_state;
    }

    [[nodiscard]] bool complete(Result<T> result) {
        const auto state = checked_state();
        {
            std::lock_guard lock(state->mutex);
            if (state->result || state->broken) {
                return false;
            }
            state->result.emplace(std::move(result));
            detail::mark_ready(*state);
        }
        state->condition.notify_all();
        return true;
    }

    void abandon() noexcept {
        if (!m_state) {
            return;
        }
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->result || m_state->broken) {
                return;
            }
            m_state->broken = true;
            detail::mark_ready(*m_state);
        }
        m_state->condition.notify_all();
    }

    std::shared_ptr<detail::FutureState<T>> m_state;
};

template <> class Promise<void> {
  public:
    Promise() : m_state(std::make_shared<detail::FutureState<void>>()) {
    }

    Promise(const Promise &) = delete;
    Promise &operator=(const Promise &) = delete;

    Promise(Promise &&other) noexcept : m_state(std::move(other.m_state)) {
    }

    Promise &operator=(Promise &&other) noexcept {
        if (this != &other) {
            abandon();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    ~Promise() {
        abandon();
    }

    [[nodiscard]] Future<void> get_future() {
        const auto state = checked_state();
        std::lock_guard lock(state->mutex);
        if (state->futureRetrieved) {
            throw std::logic_error("Promise Future was already retrieved");
        }
        state->futureRetrieved = true;
        return Future<void>(state);
    }

    [[nodiscard]] bool set_value() {
        return complete(Result<void>::success());
    }

    [[nodiscard]] bool set_error(Error error) {
        return complete(Result<void>::failure(std::move(error)));
    }

    [[nodiscard]] bool cancel() {
        return complete(Result<void>::failure(
            Error(ErrorType::Cancelled, "Future was cancelled")));
    }

    [[nodiscard]] bool set_result(Result<void> result) {
        return complete(std::move(result));
    }

  private:
    [[nodiscard]] std::shared_ptr<detail::FutureState<void>>
    checked_state() const {
        if (!m_state) {
            throw std::logic_error("operation on an invalid Promise");
        }
        return m_state;
    }

    [[nodiscard]] bool complete(Result<void> result) {
        const auto state = checked_state();
        {
            std::lock_guard lock(state->mutex);
            if (state->result || state->broken) {
                return false;
            }
            state->result.emplace(std::move(result));
            detail::mark_ready(*state);
        }
        state->condition.notify_all();
        return true;
    }

    void abandon() noexcept {
        if (!m_state) {
            return;
        }
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->result || m_state->broken) {
                return;
            }
            m_state->broken = true;
            detail::mark_ready(*m_state);
        }
        m_state->condition.notify_all();
    }

    std::shared_ptr<detail::FutureState<void>> m_state;
};

} // namespace lunaris
