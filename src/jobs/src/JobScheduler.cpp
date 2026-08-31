#include <corax/jobs/JobScheduler.h>

#include <QDeadlineTimer>
#include <QDebug>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <limits>
#include <utility>

namespace corax::jobs
{
namespace
{

using namespace std::chrono_literals;

constexpr auto shutdownDeadline = 5s;
constexpr auto shutdownDeadlineFatalCode = "jobs.shutdown_deadline_exceeded";

} // namespace

struct JobScheduler::Entry
{
    std::unique_ptr<IJob> job;
    CancellationToken cancellation;
    JobSnapshot snapshot;
    CompletionCallback completion;
    quint64 enqueueEpoch{0};
    quint64 sequence{0};
    bool completionDelivered{false};
};

JobScheduler::JobScheduler(const int maxConcurrency, QObject* parent)
    : QObject(parent), maxConcurrency_(std::max(1, maxConcurrency))
{
    qRegisterMetaType<JobSnapshot>();
    threadPool_.setMaxThreadCount(maxConcurrency_);
    threadPool_.setExpiryTimeout(30'000);
    databaseWriteThread_.setObjectName(QStringLiteral("CoraxDatabaseWriteLane"));
    databaseWriteWorker_ = new QObject;
    databaseWriteWorker_->moveToThread(&databaseWriteThread_);
    connect(&databaseWriteThread_, &QThread::finished, databaseWriteWorker_, &QObject::deleteLater);
    databaseWriteThread_.start();
}

JobScheduler::~JobScheduler()
{
    if (!waitForShutdown(shutdownDeadline))
    {
        qCritical().noquote()
            << shutdownDeadlineFatalCode
            << "A job violated the bounded shutdown contract. Corax will stop now to prevent "
               "workers from outliving scheduler state.";
        std::_Exit(EXIT_FAILURE);
    }
}

QUuid JobScheduler::submit(std::unique_ptr<IJob> job, CompletionCallback completion)
{
    assertOwnerThread();
    if (!acceptingWork_ || !job)
    {
        return {};
    }

    auto descriptor = job->descriptor();
    if (descriptor.id.isNull())
    {
        descriptor.id = QUuid::createUuid();
    }
    if (entries_.contains(descriptor.id))
    {
        return {};
    }
    if (!descriptor.cancellable && !requiresLane(descriptor, ResourceLane::DatabaseWrite))
    {
        qWarning().noquote()
            << "jobs.invalid_noncancellable_lane: A noncancellable job must use the "
               "DatabaseWrite lane. Rejected job type:"
            << descriptor.type;
        return {};
    }

    auto entry = std::make_shared<Entry>();
    entry->job = std::move(job);
    entry->cancellation = CancellationToken{std::make_shared<CancellationToken::State>()};
    entry->snapshot.descriptor = std::move(descriptor);
    entry->snapshot.state = JobState::Queued;
    entry->completion = std::move(completion);
    entry->enqueueEpoch = dispatchEpoch_;
    entry->sequence = nextSequence_++;

    const auto id = entry->snapshot.descriptor.id;
    entries_.insert(id, entry);
    pending_.append(id);
    emit jobAdded(entry->snapshot);
    dispatchAvailable();
    return id;
}

bool JobScheduler::requestCancellation(const QUuid& jobId)
{
    assertOwnerThread();
    const auto iterator = entries_.constFind(jobId);
    if (iterator == entries_.cend())
    {
        return false;
    }

    const auto& entry = iterator.value();
    if (!entry->snapshot.descriptor.cancellable || isTerminal(entry->snapshot.state))
    {
        return false;
    }

    entry->cancellation.requestCancellation();
    if (entry->snapshot.state == JobState::Queued)
    {
        removeFromPending(jobId);
        entry->snapshot.state = JobState::Canceled;
        emit jobUpdated(entry->snapshot);
        completeEntry(entry);
        dispatchAvailable();
    }
    else if (entry->snapshot.state == JobState::Running)
    {
        entry->snapshot.state = JobState::CancelRequested;
        emit jobUpdated(entry->snapshot);
    }

    return true;
}

void JobScheduler::beginShutdown()
{
    assertOwnerThread();
    if (!acceptingWork_ && shutdownSignalEmitted_)
    {
        return;
    }

    if (acceptingWork_)
    {
        acceptingWork_ = false;
        emit acceptingWorkChanged(false);
    }

    const auto queued = pending_;
    for (const auto& jobId : queued)
    {
        const auto iterator = entries_.constFind(jobId);
        if (iterator == entries_.cend())
        {
            continue;
        }
        const auto& entry = iterator.value();
        entry->cancellation.requestCancellation();
        entry->snapshot.state = JobState::Canceled;
        emit jobUpdated(entry->snapshot);
        completeEntry(entry);
    }
    pending_.clear();

    for (auto iterator = entries_.cbegin(); iterator != entries_.cend(); ++iterator)
    {
        const auto& entry = iterator.value();
        if (entry->snapshot.state == JobState::Running && entry->snapshot.descriptor.cancellable)
        {
            entry->cancellation.requestCancellation();
            entry->snapshot.state = JobState::CancelRequested;
            emit jobUpdated(entry->snapshot);
        }
    }

    if (!databaseQuitRequested_)
    {
        databaseQuitRequested_ = true;
        QMetaObject::invokeMethod(
            databaseWriteWorker_, [this] { databaseWriteThread_.quit(); }, Qt::QueuedConnection);
    }
    emitShutdownFinishedIfReady();
}

bool JobScheduler::waitForShutdown(const std::chrono::milliseconds timeout)
{
    assertOwnerThread();
    beginShutdown();

    const auto boundedTimeout =
        std::clamp<qint64>(timeout.count(), 0, std::numeric_limits<int>::max());
    QDeadlineTimer deadline{boundedTimeout};
    const bool poolStopped = threadPool_.waitForDone(deadline);
    const bool databaseStopped =
        !databaseWriteThread_.isRunning() || databaseWriteThread_.wait(deadline);
    return poolStopped && databaseStopped;
}

bool JobScheduler::isAcceptingWork() const noexcept
{
    return acceptingWork_;
}

int JobScheduler::maxConcurrency() const noexcept
{
    return maxConcurrency_;
}

int JobScheduler::activeCount() const noexcept
{
    return activeCount_;
}

std::optional<JobSnapshot> JobScheduler::snapshot(const QUuid& jobId) const
{
    assertOwnerThread();
    const auto iterator = entries_.constFind(jobId);
    if (iterator == entries_.cend())
    {
        return std::nullopt;
    }
    return iterator.value()->snapshot;
}

QVector<JobSnapshot> JobScheduler::snapshots() const
{
    assertOwnerThread();
    QVector<JobSnapshot> result;
    result.reserve(entries_.size());
    for (auto iterator = entries_.cbegin(); iterator != entries_.cend(); ++iterator)
    {
        result.append(iterator.value()->snapshot);
    }
    std::ranges::sort(result,
                      [&entries = entries_](const JobSnapshot& left, const JobSnapshot& right)
                      {
                          return entries.value(left.descriptor.id)->sequence <
                                 entries.value(right.descriptor.id)->sequence;
                      });
    return result;
}

std::shared_ptr<JobScheduler::Entry> JobScheduler::takeNextQueued()
{
    if (pending_.isEmpty())
    {
        return {};
    }

    qsizetype bestIndex = -1;
    int bestEffectivePriority = std::numeric_limits<int>::max();
    quint64 bestSequence = std::numeric_limits<quint64>::max();

    for (qsizetype index = 0; index < pending_.size(); ++index)
    {
        const auto iterator = entries_.constFind(pending_.at(index));
        if (iterator == entries_.cend() || iterator.value()->snapshot.state != JobState::Queued)
        {
            continue;
        }

        const auto& entry = iterator.value();
        if (!lanesAvailable(entry->snapshot.descriptor))
        {
            continue;
        }
        const auto basePriority = static_cast<int>(entry->snapshot.descriptor.priority);
        const auto waitedDispatches = dispatchEpoch_ - entry->enqueueEpoch;
        const auto ageBoost =
            static_cast<int>(waitedDispatches / static_cast<quint64>(agingInterval_));
        const auto effectivePriority = std::max(0, basePriority - ageBoost);

        if (effectivePriority < bestEffectivePriority ||
            (effectivePriority == bestEffectivePriority && entry->sequence < bestSequence))
        {
            bestIndex = index;
            bestEffectivePriority = effectivePriority;
            bestSequence = entry->sequence;
        }
    }

    if (bestIndex < 0)
    {
        return {};
    }

    const auto id = pending_.takeAt(bestIndex);
    return entries_.value(id);
}

void JobScheduler::dispatchAvailable()
{
    assertOwnerThread();
    while (acceptingWork_ && activeCount_ < maxConcurrency_)
    {
        auto entry = takeNextQueued();
        if (!entry)
        {
            break;
        }

        entry->snapshot.state = JobState::Running;
        acquireLanes(entry->snapshot.descriptor);
        ++activeCount_;
        ++dispatchEpoch_;
        emit activeCountChanged(activeCount_);
        emit jobUpdated(entry->snapshot);

        if (requiresLane(entry->snapshot.descriptor, ResourceLane::DatabaseWrite))
        {
            QMetaObject::invokeMethod(
                databaseWriteWorker_, [this, entry] { execute(entry); }, Qt::QueuedConnection);
        }
        else
        {
            auto runnable = QRunnable::create([this, entry] { execute(entry); });
            runnable->setAutoDelete(true);
            threadPool_.start(runnable);
        }
    }
}

void JobScheduler::execute(const std::shared_ptr<Entry>& entry)
{
    const auto jobId = entry->snapshot.descriptor.id;
    JobContext context{
        entry->cancellation,
        [this, jobId](JobProgress progress)
        {
            QMetaObject::invokeMethod(
                this,
                [this, jobId, progress = std::move(progress)]() mutable
                { handleProgress(jobId, std::move(progress)); },
                Qt::QueuedConnection);
        },
    };

    JobOutcome outcome;
    try
    {
        outcome = entry->job->run(context);
    }
    catch (const std::exception& exception)
    {
        outcome = JobOutcome::failed({
            .code = QStringLiteral("jobs.worker_exception"),
            .userMessage = tr("The background task failed."),
            .technicalContext = QString::fromUtf8(exception.what()),
            .remediation = tr("Try the task again. If it keeps failing, review the task details."),
            .retryable = true,
        });
    }
    catch (...)
    {
        outcome = JobOutcome::failed({
            .code = QStringLiteral("jobs.unknown_worker_exception"),
            .userMessage = tr("The background task failed."),
            .technicalContext = QStringLiteral("The worker threw an unknown exception."),
            .remediation = tr("Try the task again. If it keeps failing, review the task details."),
            .retryable = true,
        });
    }

    QMetaObject::invokeMethod(
        this,
        [this, jobId, outcome = std::move(outcome)]() mutable
        { finish(jobId, std::move(outcome)); },
        Qt::QueuedConnection);
}

void JobScheduler::handleProgress(const QUuid& jobId, JobProgress progress)
{
    assertOwnerThread();
    const auto iterator = entries_.constFind(jobId);
    if (iterator == entries_.cend() || isTerminal(iterator.value()->snapshot.state))
    {
        return;
    }

    progress.completedUnits = std::max<qint64>(0, progress.completedUnits);
    if (progress.totalUnits < -1)
    {
        progress.totalUnits = -1;
    }
    else if (progress.totalUnits >= 0)
    {
        progress.completedUnits = std::min(progress.completedUnits, progress.totalUnits);
    }
    progress.issueCount = std::max(0, progress.issueCount);

    iterator.value()->snapshot.progress = std::move(progress);
    emit jobUpdated(iterator.value()->snapshot);
}

void JobScheduler::finish(const QUuid& jobId, JobOutcome outcome)
{
    assertOwnerThread();
    const auto iterator = entries_.constFind(jobId);
    if (iterator == entries_.cend())
    {
        return;
    }

    const auto& entry = iterator.value();
    if (isTerminal(entry->snapshot.state))
    {
        return;
    }

    const bool cancellationWon = entry->snapshot.state == JobState::CancelRequested ||
                                 entry->cancellation.isCancellationRequested();
    if (cancellationWon || outcome.kind == JobOutcome::Kind::Canceled)
    {
        entry->snapshot.state = JobState::Canceled;
    }
    else
    {
        switch (outcome.kind)
        {
        case JobOutcome::Kind::Succeeded:
            entry->snapshot.state = JobState::Succeeded;
            break;
        case JobOutcome::Kind::SucceededWithIssues:
            entry->snapshot.state = JobState::SucceededWithIssues;
            break;
        case JobOutcome::Kind::Failed:
            entry->snapshot.state = JobState::Failed;
            break;
        case JobOutcome::Kind::Canceled:
            entry->snapshot.state = JobState::Canceled;
            break;
        }
    }

    entry->snapshot.issues = std::move(outcome.issues);
    entry->snapshot.progress.issueCount = static_cast<int>(entry->snapshot.issues.size());
    entry->snapshot.failure = std::move(outcome.failure);

    releaseLanes(entry->snapshot.descriptor);
    activeCount_ = std::max(0, activeCount_ - 1);
    emit activeCountChanged(activeCount_);
    emit jobUpdated(entry->snapshot);
    completeEntry(entry);
    dispatchAvailable();
    emitShutdownFinishedIfReady();
}

void JobScheduler::completeEntry(const std::shared_ptr<Entry>& entry)
{
    if (entry->completionDelivered)
    {
        return;
    }

    entry->completionDelivered = true;
    completionOrder_.append(entry->snapshot.descriptor.id);
    emit jobFinished(entry->snapshot);
    if (entry->completion)
    {
        entry->completion(entry->snapshot);
    }
    trimHistory();
}

void JobScheduler::removeFromPending(const QUuid& jobId)
{
    pending_.removeAll(jobId);
}

void JobScheduler::trimHistory()
{
    while (completionOrder_.size() > historyLimit_)
    {
        const auto id = completionOrder_.takeFirst();
        const auto iterator = entries_.find(id);
        if (iterator == entries_.end() || !isTerminal(iterator.value()->snapshot.state))
        {
            continue;
        }
        entries_.erase(iterator);
        emit jobRemoved(id);
    }
}

void JobScheduler::emitShutdownFinishedIfReady()
{
    if (!acceptingWork_ && activeCount_ == 0 && pending_.isEmpty() && !shutdownSignalEmitted_)
    {
        shutdownSignalEmitted_ = true;
        emit shutdownFinished();
    }
}

void JobScheduler::assertOwnerThread() const
{
    Q_ASSERT(QThread::currentThread() == thread());
}

bool JobScheduler::lanesAvailable(const JobDescriptor& descriptor) const
{
    for (int value = static_cast<int>(ResourceLane::Interactive);
         value <= static_cast<int>(ResourceLane::ExternalProcess);
         ++value)
    {
        const auto lane = static_cast<ResourceLane>(value);
        if (requiresLane(descriptor, lane) &&
            activeLaneCounts_.at(laneIndex(lane)) >= laneCapacity(lane))
        {
            return false;
        }
    }
    return true;
}

void JobScheduler::acquireLanes(const JobDescriptor& descriptor)
{
    for (int value = static_cast<int>(ResourceLane::Interactive);
         value <= static_cast<int>(ResourceLane::ExternalProcess);
         ++value)
    {
        const auto lane = static_cast<ResourceLane>(value);
        if (requiresLane(descriptor, lane))
        {
            ++activeLaneCounts_.at(laneIndex(lane));
        }
    }
}

void JobScheduler::releaseLanes(const JobDescriptor& descriptor)
{
    for (int value = static_cast<int>(ResourceLane::Interactive);
         value <= static_cast<int>(ResourceLane::ExternalProcess);
         ++value)
    {
        const auto lane = static_cast<ResourceLane>(value);
        if (requiresLane(descriptor, lane))
        {
            auto& count = activeLaneCounts_.at(laneIndex(lane));
            count = std::max(0, count - 1);
        }
    }
}

bool JobScheduler::requiresLane(const JobDescriptor& descriptor, const ResourceLane lane)
{
    return descriptor.requiredLanes.contains(lane);
}

int JobScheduler::laneCapacity(const ResourceLane lane) const noexcept
{
    switch (lane)
    {
    case ResourceLane::DatabaseWrite:
    case ResourceLane::ExternalProcess:
        return 1;
    case ResourceLane::Interactive:
    case ResourceLane::Cpu:
    case ResourceLane::Io:
    case ResourceLane::DatabaseRead:
        return maxConcurrency_;
    }
    return 1;
}

std::size_t JobScheduler::laneIndex(const ResourceLane lane) noexcept
{
    return static_cast<std::size_t>(lane);
}

} // namespace corax::jobs
