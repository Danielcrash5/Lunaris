#pragma once

#include "lunaris/Result.h"

#include <functional>

namespace lunaris {

/// Host-owned task scheduler. Lunaris does not provide a worker thread pool.
class Scheduler {
  public:
    using Task = std::function<void()>;

    virtual ~Scheduler() = default;

    /// Accepts a task for host-defined execution.
    /// Success means the task will be executed exactly once; failure means
    /// the task was not accepted. The scheduler defines its execution context.
    [[nodiscard]] virtual Result<void> schedule(Task task) = 0;
};

} // namespace lunaris
