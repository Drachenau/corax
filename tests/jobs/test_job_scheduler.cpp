#include <corax/jobs/FakeWorkUnitJob.h>
#include <corax/jobs/JobScheduler.h>

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

using namespace std::chrono_literals;

namespace
{

corax::jobs::JobDescriptor
descriptor(const QString& type,
           const corax::jobs::JobPriority priority = corax::jobs::JobPriority::Background,
           QVector<corax::jobs::ResourceLane> lanes = {corax::jobs::ResourceLane::Cpu})
{
    return {
        .id = QUuid::createUuid(),
        .type = type,
        .owner = QStringLiteral("job_scheduler_test"),
        .payloadVersion = 1,
        .title = type,
        .projectId = {},
        .priority = priority,
        .requiredLanes = std::move(lanes),
        .cancellable = true,
    };
}

class GateJob final : public corax::jobs::IJob
{
public:
    GateJob(corax::jobs::JobDescriptor descriptor, QSemaphore& started, QSemaphore& release)
        : descriptor_(std::move(descriptor)), started_(started), release_(release)
    {
    }

    const corax::jobs::JobDescriptor& descriptor() const noexcept override
    {
        return descriptor_;
    }

    corax::jobs::JobOutcome run(corax::jobs::JobContext& context) override
    {
        started_.release();
        release_.acquire();
        return context.isCancellationRequested() ? corax::jobs::JobOutcome::canceled()
                                                 : corax::jobs::JobOutcome::succeeded();
    }

private:
    corax::jobs::JobDescriptor descriptor_;
    QSemaphore& started_;
    QSemaphore& release_;
};

class CancellationWaitJob final : public corax::jobs::IJob
{
public:
    CancellationWaitJob(corax::jobs::JobDescriptor descriptor, QSemaphore& started)
        : descriptor_(std::move(descriptor)), started_(started)
    {
    }

    const corax::jobs::JobDescriptor& descriptor() const noexcept override
    {
        return descriptor_;
    }

    corax::jobs::JobOutcome run(corax::jobs::JobContext& context) override
    {
        started_.release();
        const bool canceled = context.cancellationToken().waitForCancellationFor(5s);
        return canceled ? corax::jobs::JobOutcome::canceled()
                        : corax::jobs::JobOutcome::failed({
                              .code = QStringLiteral("test.timeout"),
                              .userMessage = QStringLiteral("Test cancellation timed out."),
                              .technicalContext = {},
                              .remediation = {},
                              .retryable = false,
                          });
    }

private:
    corax::jobs::JobDescriptor descriptor_;
    QSemaphore& started_;
};

class CountingGateJob final : public corax::jobs::IJob
{
public:
    CountingGateJob(corax::jobs::JobDescriptor descriptor,
                    QSemaphore& started,
                    QSemaphore& release,
                    std::atomic_int& active,
                    std::atomic_int& maximum)
        : descriptor_(std::move(descriptor)), started_(started), release_(release), active_(active),
          maximum_(maximum)
    {
    }

    const corax::jobs::JobDescriptor& descriptor() const noexcept override
    {
        return descriptor_;
    }

    corax::jobs::JobOutcome run(corax::jobs::JobContext&) override
    {
        const int activeNow = active_.fetch_add(1) + 1;
        int observed = maximum_.load();
        while (activeNow > observed && !maximum_.compare_exchange_weak(observed, activeNow))
        {
        }
        started_.release();
        release_.acquire();
        active_.fetch_sub(1);
        return corax::jobs::JobOutcome::succeeded();
    }

private:
    corax::jobs::JobDescriptor descriptor_;
    QSemaphore& started_;
    QSemaphore& release_;
    std::atomic_int& active_;
    std::atomic_int& maximum_;
};

class RecordJob final : public corax::jobs::IJob
{
public:
    RecordJob(corax::jobs::JobDescriptor descriptor,
              QString marker,
              QStringList& order,
              QMutex& mutex)
        : descriptor_(std::move(descriptor)), marker_(std::move(marker)), order_(order),
          mutex_(mutex)
    {
    }

    const corax::jobs::JobDescriptor& descriptor() const noexcept override
    {
        return descriptor_;
    }

    corax::jobs::JobOutcome run(corax::jobs::JobContext&) override
    {
        const QMutexLocker lock{&mutex_};
        order_.append(marker_);
        return corax::jobs::JobOutcome::succeeded();
    }

private:
    corax::jobs::JobDescriptor descriptor_;
    QString marker_;
    QStringList& order_;
    QMutex& mutex_;
};

} // namespace

class JobSchedulerTest final : public QObject
{
    Q_OBJECT

private slots:
    void fakeJobReportsProgressAndSucceeds();
    void queuedJobCancelsWithoutStarting();
    void runningJobPublishesCancelRequestedThenCanceled();
    void concurrencyIsBounded();
    void queuedPriorityIsRespected();
    void databaseWriteLaneKeepsThreadAffinity();
    void shutdownCancelsCooperativeWorkAndRejectsSubmission();
    void shutdownDeadlineReturnsWithoutDetachingWorker();
    void noncancellableWorkRequiresDatabaseWriteLane();
    void failedOutcomeRetainsStructuredFailure();
    void succeededWithIssuesRetainsStructuredIssues();
    void completionCallbackRunsOnOwnerThread();
};

void JobSchedulerTest::fakeJobReportsProgressAndSucceeds()
{
    corax::jobs::JobScheduler scheduler{1};
    QSignalSpy finished{&scheduler, &corax::jobs::JobScheduler::jobFinished};
    const auto jobDescriptor = descriptor(QStringLiteral("fake"));
    const auto id =
        scheduler.submit(std::make_unique<corax::jobs::FakeWorkUnitJob>(jobDescriptor, 3, 0ms));

    QVERIFY(!id.isNull());
    QVERIFY(QTest::qWaitFor([&finished] { return finished.size() == 1; }, 2'000));
    QCOMPARE(finished.size(), 1);
    const auto snapshot = scheduler.snapshot(id);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->state, corax::jobs::JobState::Succeeded);
    QCOMPARE(snapshot->progress.completedUnits, 3);
    QCOMPARE(snapshot->progress.totalUnits, 3);
}

void JobSchedulerTest::queuedJobCancelsWithoutStarting()
{
    corax::jobs::JobScheduler scheduler{1};
    QSemaphore blockerStarted;
    QSemaphore blockerRelease;
    QSemaphore queuedStarted;
    QSemaphore queuedRelease;

    const auto blockerId = scheduler.submit(std::make_unique<GateJob>(
        descriptor(QStringLiteral("blocker")), blockerStarted, blockerRelease));
    QVERIFY(blockerStarted.tryAcquire(1, 2'000));

    const auto queuedId = scheduler.submit(std::make_unique<GateJob>(
        descriptor(QStringLiteral("queued")), queuedStarted, queuedRelease));
    QCOMPARE(scheduler.snapshot(queuedId)->state, corax::jobs::JobState::Queued);
    QVERIFY(scheduler.requestCancellation(queuedId));
    QCOMPARE(scheduler.snapshot(queuedId)->state, corax::jobs::JobState::Canceled);
    QCOMPARE(queuedStarted.available(), 0);

    blockerRelease.release();
    QVERIFY(QTest::qWaitFor(
        [&scheduler, &blockerId]
        { return scheduler.snapshot(blockerId)->state == corax::jobs::JobState::Succeeded; },
        2'000));
}

void JobSchedulerTest::runningJobPublishesCancelRequestedThenCanceled()
{
    corax::jobs::JobScheduler scheduler{1};
    QSemaphore started;
    QVector<corax::jobs::JobState> states;
    connect(&scheduler,
            &corax::jobs::JobScheduler::jobUpdated,
            this,
            [&states](const auto& snapshot) { states.append(snapshot.state); });

    const auto id = scheduler.submit(
        std::make_unique<CancellationWaitJob>(descriptor(QStringLiteral("cancel")), started));
    QVERIFY(started.tryAcquire(1, 2'000));
    QVERIFY(scheduler.requestCancellation(id));
    QCOMPARE(scheduler.snapshot(id)->state, corax::jobs::JobState::CancelRequested);
    QVERIFY(QTest::qWaitFor(
        [&scheduler, &id]
        { return scheduler.snapshot(id)->state == corax::jobs::JobState::Canceled; },
        2'000));
    QVERIFY(states.contains(corax::jobs::JobState::Running));
    QVERIFY(states.contains(corax::jobs::JobState::CancelRequested));
    QVERIFY(states.contains(corax::jobs::JobState::Canceled));
}

void JobSchedulerTest::concurrencyIsBounded()
{
    corax::jobs::JobScheduler scheduler{2};
    QSemaphore started;
    QSemaphore release;
    std::atomic_int active{0};
    std::atomic_int maximum{0};
    QVector<QUuid> ids;

    for (int index = 0; index < 4; ++index)
    {
        ids.append(scheduler.submit(
            std::make_unique<CountingGateJob>(descriptor(QStringLiteral("bounded_%1").arg(index)),
                                              started,
                                              release,
                                              active,
                                              maximum)));
    }

    QVERIFY(started.tryAcquire(2, 2'000));
    QCOMPARE(maximum.load(), 2);
    QCOMPARE(scheduler.activeCount(), 2);
    release.release(4);

    QVERIFY(QTest::qWaitFor(
        [&ids, &scheduler]
        {
            return std::ranges::all_of(ids,
                                       [&scheduler](const QUuid& id)
                                       {
                                           const auto snapshot = scheduler.snapshot(id);
                                           return snapshot && snapshot->state ==
                                                                  corax::jobs::JobState::Succeeded;
                                       });
        },
        3'000));
    QCOMPARE(maximum.load(), 2);
}

void JobSchedulerTest::queuedPriorityIsRespected()
{
    corax::jobs::JobScheduler scheduler{1};
    QSemaphore blockerStarted;
    QSemaphore blockerRelease;
    QStringList order;
    QMutex mutex;
    QSignalSpy finished{&scheduler, &corax::jobs::JobScheduler::jobFinished};

    QVERIFY(!scheduler
                 .submit(std::make_unique<GateJob>(
                     descriptor(QStringLiteral("blocker")), blockerStarted, blockerRelease))
                 .isNull());
    QVERIFY(blockerStarted.tryAcquire(1, 2'000));

    QVERIFY(
        !scheduler
             .submit(std::make_unique<RecordJob>(
                 descriptor(QStringLiteral("maintenance"), corax::jobs::JobPriority::Maintenance),
                 QStringLiteral("maintenance"),
                 order,
                 mutex))
             .isNull());
    QVERIFY(!scheduler
                 .submit(std::make_unique<RecordJob>(
                     descriptor(QStringLiteral("blocking"), corax::jobs::JobPriority::UserBlocking),
                     QStringLiteral("blocking"),
                     order,
                     mutex))
                 .isNull());

    blockerRelease.release();
    QVERIFY(QTest::qWaitFor([&finished] { return finished.size() == 3; }, 3'000));
    QCOMPARE(finished.size(), 3);
    QCOMPARE(order, QStringList({QStringLiteral("blocking"), QStringLiteral("maintenance")}));
}

void JobSchedulerTest::databaseWriteLaneKeepsThreadAffinity()
{
    corax::jobs::JobScheduler scheduler{2};
    QVector<Qt::HANDLE> threads;
    QMutex mutex;
    QSignalSpy finished{&scheduler, &corax::jobs::JobScheduler::jobFinished};

    for (int index = 0; index < 2; ++index)
    {
        auto jobDescriptor = descriptor(QStringLiteral("database_%1").arg(index),
                                        corax::jobs::JobPriority::Foreground,
                                        {corax::jobs::ResourceLane::DatabaseWrite});
        QVERIFY(!scheduler
                     .submit(std::make_unique<corax::jobs::FunctionalJob>(
                         std::move(jobDescriptor),
                         [&threads, &mutex](corax::jobs::JobContext&)
                         {
                             const QMutexLocker lock{&mutex};
                             threads.append(QThread::currentThreadId());
                             return corax::jobs::JobOutcome::succeeded();
                         }))
                     .isNull());
    }

    QVERIFY(QTest::qWaitFor([&finished] { return finished.size() == 2; }, 3'000));
    QCOMPARE(finished.size(), 2);
    QCOMPARE(threads.size(), 2);
    QCOMPARE(threads.at(0), threads.at(1));
    QVERIFY(threads.at(0) != QThread::currentThreadId());
}

void JobSchedulerTest::shutdownCancelsCooperativeWorkAndRejectsSubmission()
{
    corax::jobs::JobScheduler scheduler{2};
    QSemaphore started;
    QSignalSpy shutdownFinished{&scheduler, &corax::jobs::JobScheduler::shutdownFinished};
    QVector<QUuid> ids;
    for (int index = 0; index < 2; ++index)
    {
        ids.append(scheduler.submit(std::make_unique<CancellationWaitJob>(
            descriptor(QStringLiteral("shutdown_%1").arg(index)), started)));
    }
    QVERIFY(started.tryAcquire(2, 2'000));

    scheduler.beginShutdown();
    QVERIFY(!scheduler.isAcceptingWork());
    QVERIFY(scheduler
                .submit(std::make_unique<corax::jobs::FakeWorkUnitJob>(
                    descriptor(QStringLiteral("rejected")), 1, 0ms))
                .isNull());
    QVERIFY(QTest::qWaitFor([&shutdownFinished] { return shutdownFinished.size() == 1; }, 3'000));
    QCOMPARE(shutdownFinished.size(), 1);
    for (const auto& id : ids)
    {
        QCOMPARE(scheduler.snapshot(id)->state, corax::jobs::JobState::Canceled);
    }
}

void JobSchedulerTest::shutdownDeadlineReturnsWithoutDetachingWorker()
{
    corax::jobs::JobScheduler scheduler{1};
    QSemaphore started;
    QSemaphore release;
    auto jobDescriptor = descriptor(QStringLiteral("shutdown_deadline"));
    jobDescriptor.cancellable = false;
    jobDescriptor.requiredLanes = {corax::jobs::ResourceLane::DatabaseWrite};
    const auto id =
        scheduler.submit(std::make_unique<GateJob>(std::move(jobDescriptor), started, release));
    QVERIFY(!id.isNull());
    QVERIFY(started.tryAcquire(1, 2'000));

    QVERIFY(!scheduler.waitForShutdown(0ms));
    QVERIFY(scheduler.snapshot(id)->state == corax::jobs::JobState::Running);

    release.release();
    QVERIFY(scheduler.waitForShutdown(2s));
    QVERIFY(QTest::qWaitFor(
        [&scheduler, &id]
        { return scheduler.snapshot(id)->state == corax::jobs::JobState::Succeeded; },
        2'000));
}

void JobSchedulerTest::noncancellableWorkRequiresDatabaseWriteLane()
{
    corax::jobs::JobScheduler scheduler{1};
    auto jobDescriptor = descriptor(QStringLiteral("invalid_noncancellable"));
    jobDescriptor.cancellable = false;
    QTest::ignoreMessage(
        QtWarningMsg,
        "jobs.invalid_noncancellable_lane: A noncancellable job must use the DatabaseWrite lane. "
        "Rejected job type: invalid_noncancellable");

    const auto id = scheduler.submit(std::make_unique<corax::jobs::FunctionalJob>(
        std::move(jobDescriptor),
        [](corax::jobs::JobContext&) { return corax::jobs::JobOutcome::succeeded(); }));

    QVERIFY(id.isNull());
    QCOMPARE(scheduler.snapshots().size(), 0);
}

void JobSchedulerTest::failedOutcomeRetainsStructuredFailure()
{
    corax::jobs::JobScheduler scheduler{1};
    const corax::jobs::JobFailure expected{
        .code = QStringLiteral("test.structured_failure"),
        .userMessage = QStringLiteral("The test job failed."),
        .technicalContext = QStringLiteral("Synthetic failure context."),
        .remediation = QStringLiteral("Retry the synthetic operation."),
        .retryable = true,
    };
    const auto id = scheduler.submit(std::make_unique<corax::jobs::FunctionalJob>(
        descriptor(QStringLiteral("failure")),
        [expected](corax::jobs::JobContext&)
        { return corax::jobs::JobOutcome::failed(expected); }));
    QVERIFY(!id.isNull());

    QVERIFY(
        QTest::qWaitFor([&scheduler, &id]
                        { return scheduler.snapshot(id)->state == corax::jobs::JobState::Failed; },
                        2'000));
    const auto snapshot = scheduler.snapshot(id);
    QCOMPARE(snapshot->failure.code, expected.code);
    QCOMPARE(snapshot->failure.userMessage, expected.userMessage);
    QCOMPARE(snapshot->failure.technicalContext, expected.technicalContext);
    QCOMPARE(snapshot->failure.remediation, expected.remediation);
    QCOMPARE(snapshot->failure.retryable, expected.retryable);
}

void JobSchedulerTest::succeededWithIssuesRetainsStructuredIssues()
{
    corax::jobs::JobScheduler scheduler{1};
    const corax::jobs::JobIssue expected{
        .code = QStringLiteral("test.item_warning"),
        .severity = corax::jobs::JobIssueSeverity::ItemFailure,
        .userMessage = QStringLiteral("One synthetic item failed."),
        .technicalContext = QStringLiteral("Synthetic item context."),
        .affectedObjectId = QStringLiteral("asset-test-1"),
        .retryable = true,
    };
    const auto id = scheduler.submit(std::make_unique<corax::jobs::FunctionalJob>(
        descriptor(QStringLiteral("succeeded_with_issues")),
        [expected](corax::jobs::JobContext&)
        { return corax::jobs::JobOutcome::succeededWithIssues({expected}); }));
    QVERIFY(!id.isNull());

    QVERIFY(QTest::qWaitFor(
        [&scheduler, &id]
        { return scheduler.snapshot(id)->state == corax::jobs::JobState::SucceededWithIssues; },
        2'000));
    const auto snapshot = scheduler.snapshot(id);
    QCOMPARE(snapshot->progress.issueCount, 1);
    QCOMPARE(snapshot->issues.size(), 1);
    QCOMPARE(snapshot->issues.constFirst().code, expected.code);
    QCOMPARE(snapshot->issues.constFirst().severity, expected.severity);
    QCOMPARE(snapshot->issues.constFirst().userMessage, expected.userMessage);
    QCOMPARE(snapshot->issues.constFirst().technicalContext, expected.technicalContext);
    QCOMPARE(snapshot->issues.constFirst().affectedObjectId, expected.affectedObjectId);
    QCOMPARE(snapshot->issues.constFirst().retryable, expected.retryable);
}

void JobSchedulerTest::completionCallbackRunsOnOwnerThread()
{
    corax::jobs::JobScheduler scheduler{1};
    QThread* callbackThread = nullptr;
    bool called = false;
    QVERIFY(!scheduler
                 .submit(std::make_unique<corax::jobs::FakeWorkUnitJob>(
                             descriptor(QStringLiteral("callback")), 1, 0ms),
                         [&callbackThread, &called](const auto&)
                         {
                             callbackThread = QThread::currentThread();
                             called = true;
                         })
                 .isNull());

    QVERIFY(QTest::qWaitFor([&called] { return called; }, 2'000));
    QCOMPARE(callbackThread, QCoreApplication::instance()->thread());
}

QTEST_GUILESS_MAIN(JobSchedulerTest)

#include "test_job_scheduler.moc"
