#include "lunaris/State.h"

#include <lua.h>

#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace lunaris {

struct State::AllocatorContext {
    Allocator::AllocateFn allocate = nullptr;
    void *userData = nullptr;
    std::optional<std::size_t> memoryLimit;
    std::size_t allocatedBytes = 0;
};

namespace {

void *default_allocate(void *, void *pointer, std::size_t,
                       std::size_t newSize) {
    if (newSize == 0) {
        std::free(pointer);
        return nullptr;
    }

    return std::realloc(pointer, newSize);
}

} // namespace

void *State::tracked_allocate(void *userData, void *pointer,
                              std::size_t oldSize, std::size_t newSize) {
    auto &context = *static_cast<AllocatorContext *>(userData);
    const std::size_t previousSize = pointer != nullptr ? oldSize : 0;

    if (newSize == 0) {
        void *result =
            context.allocate(context.userData, pointer, oldSize, newSize);
        context.allocatedBytes -= previousSize;
        return result;
    }

    if (previousSize > context.allocatedBytes) {
        return nullptr;
    }

    const std::size_t retainedBytes =
        context.allocatedBytes - previousSize;
    if (newSize > std::numeric_limits<std::size_t>::max() - retainedBytes) {
        return nullptr;
    }

    if (context.memoryLimit.has_value() &&
        (retainedBytes > *context.memoryLimit ||
         newSize > *context.memoryLimit - retainedBytes)) {
        return nullptr;
    }

    void *result =
        context.allocate(context.userData, pointer, oldSize, newSize);
    if (result != nullptr) {
        context.allocatedBytes = retainedBytes + newSize;
    }

    return result;
}

State::State(const StateConfig &config) {
    m_allocatorContext = std::make_unique<AllocatorContext>();
    m_allocatorContext->allocate = config.allocator.allocate != nullptr
                                      ? config.allocator.allocate
                                      : &default_allocate;
    m_allocatorContext->userData = config.allocator.userData;
    m_allocatorContext->memoryLimit = config.memoryLimit;

    m_state = lua_newstate(&State::tracked_allocate, m_allocatorContext.get());
    if (m_state == nullptr) {
        throw std::bad_alloc{};
    }
}

State::~State() {
    if (m_state != nullptr) {
        lua_close(m_state);
    }
}

State::State(State &&other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)),
      m_allocatorContext(std::move(other.m_allocatorContext)) {
}

State &State::operator=(State &&other) noexcept {
    if (this != &other) {
        if (m_state != nullptr) {
            lua_close(m_state);
        }

        m_allocatorContext = std::move(other.m_allocatorContext);
        m_state = std::exchange(other.m_state, nullptr);
    }

    return *this;
}

lua_State *State::native_handle() noexcept {
    return m_state;
}

} // namespace lunaris
