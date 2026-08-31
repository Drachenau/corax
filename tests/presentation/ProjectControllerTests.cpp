// SPDX-License-Identifier: Apache-2.0

#include <corax/application/IProjectStore.h>
#include <corax/application/ProjectService.h>
#include <corax/presentation/ProjectController.h>

#include <QDateTime>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace corax::presentation
{

class ProjectControllerTestAccess final
{
public:
    static void setWorkerEntryHook(ProjectController& controller,
                                   std::function<void(QStringView)> hook)
    {
        controller.workerEntryHook_ = std::move(hook);
    }
};

} // namespace corax::presentation

namespace
{

using namespace std::chrono_literals;

constexpr auto kProjectId = "27a32f84-d3f1-40d4-938d-236315745d83";

corax::domain::ProjectInfo projectInfo(const QString& path)
{
    return {
        .projectId = QUuid(QString::fromLatin1(kProjectId)),
        .displayName = QStringLiteral("Presentation Fixture"),
        .projectPath = path,
        .createdAtUtc =
            QDateTime::fromString(QStringLiteral("2026-08-31T12:00:00.000Z"), Qt::ISODateWithMs),
        .revision = 0,
    };
}

class FakeProjectStore final : public corax::application::IProjectStore
{
public:
    corax::domain::Result<corax::domain::ProjectInfo>
    createProject(const corax::application::NewProject& project) override
    {
        ++createCalls;
        knownProject_ = {
            .projectId = project.projectId,
            .displayName = project.displayName,
            .projectPath = project.projectPath,
            .createdAtUtc = project.createdAtUtc,
            .revision = 0,
        };
        currentProject_ = knownProject_;
        return corax::domain::Result<corax::domain::ProjectInfo>::success(*currentProject_);
    }

    corax::domain::Result<corax::domain::ProjectInfo>
    openProject(const QString& projectDirectory) override
    {
        ++openCalls;
        if (!knownProject_ || knownProject_->projectPath != projectDirectory)
        {
            return corax::domain::Result<corax::domain::ProjectInfo>::failure({
                .code = corax::domain::ErrorCode::ProjectPathInvalid,
                .userMessage = QStringLiteral("The fixture project is unavailable."),
                .technicalContext = QStringLiteral("FakeProjectStore has no matching project."),
                .remediation = QStringLiteral("Use the seeded fixture path."),
                .affectedPath = projectDirectory,
                .retryable = false,
            });
        }
        currentProject_ = knownProject_;
        return corax::domain::Result<corax::domain::ProjectInfo>::success(*currentProject_);
    }

    corax::domain::Result<void> closeProject() override
    {
        ++closeCalls;
        currentProject_.reset();
        return corax::domain::Result<void>::success();
    }

    std::optional<corax::domain::ProjectInfo> currentProject() const override
    {
        return currentProject_;
    }

    bool recoveryRequired() const noexcept override
    {
        return false;
    }

    void seed(const QString& path)
    {
        knownProject_ = projectInfo(path);
    }

    int createCalls{0};
    int openCalls{0};
    int closeCalls{0};

private:
    std::optional<corax::domain::ProjectInfo> knownProject_;
    std::optional<corax::domain::ProjectInfo> currentProject_;
};

corax::jobs::JobDescriptor blockingDescriptor()
{
    return {
        .id = QUuid::createUuid(),
        .type = QStringLiteral("test.block_database_writer"),
        .owner = QStringLiteral("corax_presentation_tests"),
        .payloadVersion = 1,
        .title = QStringLiteral("Block database writer"),
        .projectId = {},
        .priority = corax::jobs::JobPriority::UserBlocking,
        .requiredLanes = {corax::jobs::ResourceLane::DatabaseWrite},
        .cancellable = false,
    };
}

void startOperation(const QString& operation,
                    corax::presentation::ProjectController& controller,
                    const QString& path)
{
    if (operation == QStringLiteral("project.create"))
    {
        controller.createProject(QUrl::fromLocalFile(path), QStringLiteral("Created Fixture"));
    }
    else if (operation == QStringLiteral("project.open"))
    {
        controller.openProject(QUrl::fromLocalFile(path));
    }
    else
    {
        controller.closeProject();
    }
}

} // namespace

class ProjectControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void schedulerFailureIsStructured_data();
    void schedulerFailureIsStructured();
    void queuedShutdownCancellationIsStructured_data();
    void queuedShutdownCancellationIsStructured();
    void successfulLifecycleRemainsAsynchronous();
};

void ProjectControllerTests::schedulerFailureIsStructured_data()
{
    QTest::addColumn<QString>("operation");
    QTest::newRow("create") << QStringLiteral("project.create");
    QTest::newRow("open") << QStringLiteral("project.open");
    QTest::newRow("close") << QStringLiteral("project.close");
}

void ProjectControllerTests::schedulerFailureIsStructured()
{
    QFETCH(QString, operation);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path =
        temporaryDirectory.filePath(QStringLiteral("PresentationWorkerFailure.corax"));

    FakeProjectStore store;
    store.seed(path);
    corax::application::ProjectService service{store};
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::ProjectController controller{service, scheduler};

    if (operation == QStringLiteral("project.close"))
    {
        controller.openProject(QUrl::fromLocalFile(path));
        QVERIFY(QTest::qWaitFor([&controller] { return controller.hasProject(); }, 3'000));
    }

    corax::presentation::ProjectControllerTestAccess::setWorkerEntryHook(
        controller,
        [operation](const QStringView actualOperation)
        {
            if (actualOperation == operation)
            {
                throw std::runtime_error{"Injected project worker failure."};
            }
        });

    QSignalSpy busyChanges{&controller, &corax::presentation::ProjectController::busyChanged};
    QSignalSpy opened{&controller, &corax::presentation::ProjectController::projectOpened};
    QSignalSpy closed{&controller, &corax::presentation::ProjectController::projectClosed};

    startOperation(operation, controller, path);
    QVERIFY(controller.busy());
    QVERIFY(QTest::qWaitFor([&controller] { return controller.hasError() && !controller.busy(); },
                            3'000));

    QCOMPARE(busyChanges.size(), 2);
    QCOMPARE(controller.errorCode(), QStringLiteral("project.operation_failed"));
    QCOMPARE(controller.errorMessage(), QStringLiteral("The background task failed."));
    QVERIFY(controller.errorTechnicalContext().contains(QStringLiteral("jobs.worker_exception")));
    QVERIFY(controller.errorTechnicalContext().contains(
        QStringLiteral("Injected project worker failure.")));
    QVERIFY(!controller.errorRemediation().isEmpty());
    QCOMPARE(controller.errorAffectedPath(), path);
    QVERIFY(controller.errorRetryable());
    QCOMPARE(opened.size(), 0);
    QCOMPARE(closed.size(), 0);
    QCOMPARE(controller.hasProject(), operation == QStringLiteral("project.close"));
}

void ProjectControllerTests::queuedShutdownCancellationIsStructured_data()
{
    schedulerFailureIsStructured_data();
}

void ProjectControllerTests::queuedShutdownCancellationIsStructured()
{
    QFETCH(QString, operation);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path =
        temporaryDirectory.filePath(QStringLiteral("PresentationShutdownCancellation.corax"));

    FakeProjectStore store;
    store.seed(path);
    corax::application::ProjectService service{store};
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::ProjectController controller{service, scheduler};

    if (operation == QStringLiteral("project.close"))
    {
        controller.openProject(QUrl::fromLocalFile(path));
        QVERIFY(QTest::qWaitFor([&controller] { return controller.hasProject(); }, 3'000));
    }

    QSemaphore blockerStarted;
    QSemaphore releaseBlocker;
    const auto blockerId = scheduler.submit(std::make_unique<corax::jobs::FunctionalJob>(
        blockingDescriptor(),
        [&blockerStarted, &releaseBlocker](corax::jobs::JobContext&)
        {
            blockerStarted.release();
            releaseBlocker.acquire();
            return corax::jobs::JobOutcome::succeeded();
        }));
    QVERIFY(!blockerId.isNull());
    QVERIFY(blockerStarted.tryAcquire(1, 3'000));

    QSignalSpy busyChanges{&controller, &corax::presentation::ProjectController::busyChanged};
    QSignalSpy opened{&controller, &corax::presentation::ProjectController::projectOpened};
    QSignalSpy closed{&controller, &corax::presentation::ProjectController::projectClosed};

    startOperation(operation, controller, path);
    QVERIFY(controller.busy());
    scheduler.beginShutdown();

    QVERIFY(!controller.busy());
    QCOMPARE(busyChanges.size(), 2);
    QCOMPARE(controller.errorCode(), QStringLiteral("project.operation_canceled"));
    QVERIFY(!controller.errorMessage().isEmpty());
    QVERIFY(controller.errorTechnicalContext().contains(operation));
    QVERIFY(!controller.errorRemediation().isEmpty());
    QCOMPARE(controller.errorAffectedPath(), path);
    QVERIFY(controller.errorRetryable());
    QCOMPARE(opened.size(), 0);
    QCOMPARE(closed.size(), 0);
    QCOMPARE(controller.hasProject(), operation == QStringLiteral("project.close"));

    releaseBlocker.release();
    QTRY_COMPARE(scheduler.activeCount(), 0);
    QVERIFY(scheduler.waitForShutdown(3s));
}

void ProjectControllerTests::successfulLifecycleRemainsAsynchronous()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("PresentationSuccess.corax"));
    FakeProjectStore store;
    corax::application::ProjectService service{store};
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::ProjectController controller{service, scheduler};
    QSignalSpy opened{&controller, &corax::presentation::ProjectController::projectOpened};
    QSignalSpy closed{&controller, &corax::presentation::ProjectController::projectClosed};

    controller.createProject(QUrl::fromLocalFile(path), QStringLiteral("Success Fixture"));
    QVERIFY(controller.busy());
    QVERIFY(QTest::qWaitFor([&opened] { return opened.size() == 1; }, 3'000));
    QVERIFY(controller.hasProject());
    QVERIFY(!controller.busy());
    QVERIFY(!controller.hasError());

    controller.closeProject();
    QVERIFY(controller.busy());
    QVERIFY(QTest::qWaitFor([&closed] { return closed.size() == 1; }, 3'000));
    QVERIFY(!controller.hasProject());
    QVERIFY(!controller.busy());

    controller.openProject(QUrl::fromLocalFile(path));
    QVERIFY(controller.busy());
    QVERIFY(QTest::qWaitFor([&opened] { return opened.size() == 2; }, 3'000));
    QVERIFY(controller.hasProject());
    QVERIFY(!controller.busy());
    QVERIFY(!controller.recoveryRequired());
}

QTEST_GUILESS_MAIN(ProjectControllerTests)

#include "ProjectControllerTests.moc"
