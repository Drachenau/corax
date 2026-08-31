#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace corax::jobs
{

class JobContext;
class JobScheduler;

class CancellationToken final
{
public:
    CancellationToken() = default;

    [[nodiscard]] bool isCancellationRequested() const noexcept;

    template<class Rep, class Period>
    [[nodiscard]] bool
    waitForCancellationFor(const std::chrono::duration<Rep, Period>& duration) const
    {
        if (!state_)
        {
            return false;
        }

        std::unique_lock lock{state_->mutex};
        return state_->condition.wait_for(
            lock, duration, [this] { return state_->requested.load(std::memory_order_acquire); });
    }

private:
    struct State
    {
        std::atomic_bool requested{false};
        mutable std::mutex mutex;
        mutable std::condition_variable condition;
    };

    explicit CancellationToken(std::shared_ptr<State> state);
    void requestCancellation() const noexcept;

    std::shared_ptr<State> state_;

    friend class JobContext;
    friend class JobScheduler;
};

} // namespace corax::jobs
