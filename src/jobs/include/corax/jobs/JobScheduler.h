#pragma once

#include <corax/jobs/Job.h>

#include <QHash>
#include <QObject>
#include <QThread>
#include <QThreadPool>
#include <QVector>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace corax::jobs
{

class JobScheduler final : public QObject
{
    Q_OBJECT

public:
    using CompletionCallback = std::function<void(const JobSnapshot&)>;

    explicit JobScheduler(int maxConcurrency, QObject* parent = nullptr);
    ~JobScheduler() override;

    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    [[nodiscard]] QUuid submit(std::unique_ptr<IJob> job, CompletionCallback completion = {});
    [[nodiscard]] bool requestCancellation(const QUuid& jobId);

    void beginShutdown();

    // Starts shutdown if needed and waits for both executors for at most timeout.
    // A false result means this scheduler must remain alive while its workers finish.
    // Destruction applies a five-second hard deadline and then uses a fail-stop.
    [[nodiscard]] bool waitForShutdown(std::chrono::milliseconds timeout);

    [[nodiscard]] bool isAcceptingWork() const noexcept;
    [[nodiscard]] int maxConcurrency() const noexcept;
    [[nodiscard]] int activeCount() const noexcept;
    [[nodiscard]] std::optional<JobSnapshot> snapshot(const QUuid& jobId) const;
    [[nodiscard]] QVector<JobSnapshot> snapshots() const;

signals:
    void jobAdded(const corax::jobs::JobSnapshot& snapshot);
    void jobUpdated(const corax::jobs::JobSnapshot& snapshot);
    void jobFinished(const corax::jobs::JobSnapshot& snapshot);
    void jobRemoved(const QUuid& jobId);
    void acceptingWorkChanged(bool accepting);
    void activeCountChanged(int activeCount);
    void shutdownFinished();

private:
    struct Entry;

    [[nodiscard]] std::shared_ptr<Entry> takeNextQueued();
    void dispatchAvailable();
    void execute(const std::shared_ptr<Entry>& entry);
    void handleProgress(const QUuid& jobId, JobProgress progress);
    void finish(const QUuid& jobId, JobOutcome outcome);
    void completeEntry(const std::shared_ptr<Entry>& entry);
    void removeFromPending(const QUuid& jobId);
    void trimHistory();
    void emitShutdownFinishedIfReady();
    void assertOwnerThread() const;
    [[nodiscard]] bool lanesAvailable(const JobDescriptor& descriptor) const;
    void acquireLanes(const JobDescriptor& descriptor);
    void releaseLanes(const JobDescriptor& descriptor);
    [[nodiscard]] static bool requiresLane(const JobDescriptor& descriptor, ResourceLane lane);
    [[nodiscard]] int laneCapacity(ResourceLane lane) const noexcept;
    [[nodiscard]] static std::size_t laneIndex(ResourceLane lane) noexcept;

    QThreadPool threadPool_;
    QThread databaseWriteThread_;
    QObject* databaseWriteWorker_{nullptr};
    QHash<QUuid, std::shared_ptr<Entry>> entries_;
    QVector<QUuid> pending_;
    QVector<QUuid> completionOrder_;
    int maxConcurrency_{1};
    int activeCount_{0};
    bool acceptingWork_{true};
    bool shutdownSignalEmitted_{false};
    bool databaseQuitRequested_{false};
    quint64 dispatchEpoch_{0};
    quint64 nextSequence_{0};
    int agingInterval_{8};
    int historyLimit_{128};
    std::array<int, 6> activeLaneCounts_{};
};

} // namespace corax::jobs
