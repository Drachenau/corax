#include <corax/jobs/CancellationToken.h>

#include <utility>

namespace corax::jobs
{

CancellationToken::CancellationToken(std::shared_ptr<State> state) : state_(std::move(state)) {}

bool CancellationToken::isCancellationRequested() const noexcept
{
    return state_ && state_->requested.load(std::memory_order_acquire);
}

void CancellationToken::requestCancellation() const noexcept
{
    if (!state_)
    {
        return;
    }

    {
        const std::lock_guard lock{state_->mutex};
        state_->requested.store(true, std::memory_order_release);
    }
    state_->condition.notify_all();
}

} // namespace corax::jobs
