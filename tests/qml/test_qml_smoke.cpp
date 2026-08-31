#include <corax/application/IProjectStore.h>
#include <corax/application/ProjectService.h>
#include <corax/domain/AppError.h>
#include <corax/presentation/JobsController.h>
#include <corax/presentation/ProjectController.h>

#include <QDateTime>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QVariant>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

Q_IMPORT_QML_PLUGIN(Corax_UiPlugin)

namespace
{

QQuickItem* findVisualItem(QQuickItem* parent, const QString& objectName)
{
    if (!parent)
    {
        return nullptr;
    }
    if (parent->objectName() == objectName)
    {
        return parent;
    }
    for (auto* child : parent->childItems())
    {
        if (auto* match = findVisualItem(child, objectName))
        {
            return match;
        }
    }
    return nullptr;
}

class FakeProjectStore final : public corax::application::IProjectStore
{
public:
    corax::domain::Result<corax::domain::ProjectInfo>
    createProject(const corax::application::NewProject& project) override
    {
        recordThread();
        knownProject_ = corax::domain::ProjectInfo{
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
        recordThread();
        if (throwOpen_)
        {
            throw std::runtime_error{"Injected open-store exception."};
        }
        if (failOpen_)
        {
            return corax::domain::Result<corax::domain::ProjectInfo>::failure({
                .code = corax::domain::ErrorCode::ProjectIdentityMismatch,
                .userMessage = QStringLiteral("The project identity does not match."),
                .technicalContext = QStringLiteral("Manifest ID differs from database ID."),
                .remediation = QStringLiteral("Open a matching project copy or restore a backup."),
                .affectedPath = projectDirectory,
                .retryable = false,
            });
        }
        if (!knownProject_ || knownProject_->projectPath != projectDirectory)
        {
            return corax::domain::Result<corax::domain::ProjectInfo>::failure({
                .code = corax::domain::ErrorCode::ProjectPathInvalid,
                .userMessage = QStringLiteral("The project could not be opened."),
                .technicalContext = QStringLiteral("The fake project does not exist."),
                .remediation = QStringLiteral("Choose the created test project."),
                .affectedPath = projectDirectory,
                .retryable = false,
            });
        }
        currentProject_ = knownProject_;
        return corax::domain::Result<corax::domain::ProjectInfo>::success(*currentProject_);
    }

    corax::domain::Result<void> closeProject() override
    {
        recordThread();
        if (throwClose_)
        {
            throw std::runtime_error{"Injected close-store exception."};
        }
        currentProject_.reset();
        return corax::domain::Result<void>::success();
    }

    std::optional<corax::domain::ProjectInfo> currentProject() const override
    {
        return currentProject_;
    }

    void setFailOpen(const bool fail)
    {
        failOpen_ = fail;
    }
    void setThrowOpen(const bool shouldThrow)
    {
        throwOpen_ = shouldThrow;
    }
    void setThrowClose(const bool shouldThrow)
    {
        throwClose_ = shouldThrow;
    }
    [[nodiscard]] Qt::HANDLE operationThread() const noexcept
    {
        return operationThread_;
    }
    [[nodiscard]] bool threadChanged() const noexcept
    {
        return threadChanged_;
    }

private:
    void recordThread()
    {
        const auto current = QThread::currentThreadId();
        if (operationThread_ && operationThread_ != current)
        {
            threadChanged_ = true;
        }
        operationThread_ = current;
    }

    std::optional<corax::domain::ProjectInfo> knownProject_;
    std::optional<corax::domain::ProjectInfo> currentProject_;
    Qt::HANDLE operationThread_{nullptr};
    bool threadChanged_{false};
    bool failOpen_{false};
    bool throwOpen_{false};
    bool throwClose_{false};
};

} // namespace

class QmlSmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void mainShellLoadsAndProjectLifecycleIsAsynchronous();
    void rejectedFakeJobSubmissionReturnsEmptyId();
    void structuredProjectErrorReachesPresentation();
    void dependencyExceptionsReachPresentation();
    void structuredJobOutcomesReachLoadedQml();
};

void QmlSmokeTest::mainShellLoadsAndProjectLifecycleIsAsynchronous()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const auto projectPath = temporaryDirectory.filePath(QStringLiteral("Demo.corax"));
    const QUuid projectId{QStringLiteral("{27a32f84-d3f1-40d4-938d-236315745d83}")};
    const QDateTime createdAt{QDate(2026, 8, 30), QTime(12, 0), QTimeZone::UTC};

    FakeProjectStore store;
    corax::application::ProjectService service{
        store, [projectId] { return projectId; }, [createdAt] { return createdAt; }};
    corax::jobs::JobScheduler scheduler{2};
    corax::presentation::ProjectController projectController{service, scheduler};
    corax::presentation::JobsController jobsController{scheduler};

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("projectController"), QVariant::fromValue(&projectController)},
        {QStringLiteral("jobsController"), QVariant::fromValue(&jobsController)},
    });
    engine.loadFromModule(QStringLiteral("Corax.Ui"), QStringLiteral("Main"));
    QTRY_COMPARE_WITH_TIMEOUT(engine.rootObjects().size(), 1, 3'000);

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QCOMPARE(window->objectName(), QStringLiteral("mainWindow"));
    QVERIFY(window->findChild<QObject*>(QStringLiteral("projectTitle")) != nullptr);

    QSignalSpy opened{&projectController, &corax::presentation::ProjectController::projectOpened};
    projectController.createProject(QUrl::fromLocalFile(projectPath), QStringLiteral("Demo"));
    QVERIFY(projectController.busy());
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 3'000);
    QVERIFY(projectController.hasProject());
    QCOMPARE(projectController.projectName(), QStringLiteral("Demo"));
    QCOMPARE(projectController.projectId(), projectId.toString(QUuid::WithoutBraces));
    QVERIFY(!projectController.busy());

    QSignalSpy closed{&projectController, &corax::presentation::ProjectController::projectClosed};
    projectController.closeProject();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 3'000);
    QVERIFY(!projectController.hasProject());

    projectController.openProject(QUrl::fromLocalFile(projectPath));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 3'000);
    QCOMPARE(projectController.projectId(), projectId.toString(QUuid::WithoutBraces));
    QVERIFY(store.operationThread() != QThread::currentThreadId());
    QVERIFY(!store.threadChanged());

    const auto fakeJobId = jobsController.startFakeJob();
    QVERIFY(!QUuid::fromString(fakeJobId).isNull());
    QTRY_VERIFY_WITH_TIMEOUT(jobsController.jobs()->rowCount() > 0, 2'000);
}

void QmlSmokeTest::rejectedFakeJobSubmissionReturnsEmptyId()
{
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::JobsController jobsController{scheduler};

    scheduler.beginShutdown();

    QVERIFY(!jobsController.acceptingWork());
    QCOMPARE(jobsController.startFakeJob(), QString{});
    QCOMPARE(jobsController.jobs()->rowCount(), 0);
}

void QmlSmokeTest::structuredProjectErrorReachesPresentation()
{
    FakeProjectStore store;
    store.setFailOpen(true);
    corax::application::ProjectService service{store};
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::ProjectController controller{service, scheduler};

    QSignalSpy errors{&controller, &corax::presentation::ProjectController::errorChanged};
    const auto path = QStringLiteral("/tmp/identity-mismatch.corax");
    controller.openProject(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasError(), 3'000);
    QVERIFY(errors.size() >= 1);
    QCOMPARE(controller.errorCode(), QStringLiteral("project.identity_mismatch"));
    QCOMPARE(controller.errorMessage(), QStringLiteral("The project identity does not match."));
    QCOMPARE(controller.errorTechnicalContext(),
             QStringLiteral("Manifest ID differs from database ID."));
    QCOMPARE(controller.errorRemediation(),
             QStringLiteral("Open a matching project copy or restore a backup."));
    QCOMPARE(controller.errorAffectedPath(), path);
}

void QmlSmokeTest::dependencyExceptionsReachPresentation()
{
    FakeProjectStore store;
    corax::application::ProjectService service{store};
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::ProjectController controller{service, scheduler};

    store.setThrowOpen(true);
    controller.openProject(QUrl::fromLocalFile(QStringLiteral("/tmp/throw-open.corax")));
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasError() && !controller.busy(), 3'000);
    QCOMPARE(controller.errorCode(), QStringLiteral("core.internal_error"));
    QCOMPARE(controller.errorMessage(),
             QStringLiteral("Corax could not prepare the project operation."));
    QCOMPARE(controller.errorTechnicalContext(), QStringLiteral("Injected open-store exception."));
    QCOMPARE(controller.errorRemediation(),
             QStringLiteral("Restart Corax and try the operation again."));
    QVERIFY(!controller.errorRetryable());

    store.setThrowOpen(false);
    store.setThrowClose(true);
    controller.clearError();
    controller.closeProject();
    QTRY_VERIFY_WITH_TIMEOUT(controller.hasError() && !controller.busy(), 3'000);
    QCOMPARE(controller.errorCode(), QStringLiteral("core.internal_error"));
    QCOMPARE(controller.errorMessage(),
             QStringLiteral("Corax could not prepare the project operation."));
    QCOMPARE(controller.errorTechnicalContext(), QStringLiteral("Injected close-store exception."));
    QCOMPARE(controller.errorRemediation(),
             QStringLiteral("Restart Corax and try the operation again."));
    QVERIFY(!controller.errorRetryable());
}

void QmlSmokeTest::structuredJobOutcomesReachLoadedQml()
{
    FakeProjectStore store;
    corax::application::ProjectService service{store};
    corax::jobs::JobScheduler scheduler{1};
    corax::presentation::ProjectController projectController{service, scheduler};
    corax::presentation::JobsController jobsController{scheduler};

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("projectController"), QVariant::fromValue(&projectController)},
        {QStringLiteral("jobsController"), QVariant::fromValue(&jobsController)},
    });
    engine.loadFromModule(QStringLiteral("Corax.Ui"), QStringLiteral("Main"));
    QTRY_COMPARE_WITH_TIMEOUT(engine.rootObjects().size(), 1, 3'000);

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);

    const corax::jobs::JobDescriptor failureDescriptor{
        .id = QUuid{QStringLiteral("{5dfc09fb-4cfb-4473-a1c1-5b9e8d6c655e}")},
        .type = QStringLiteral("test.presentation_failure"),
        .owner = QStringLiteral("qml_smoke_test"),
        .payloadVersion = 1,
        .title = QStringLiteral("Presentation failure test"),
        .projectId = {},
        .priority = corax::jobs::JobPriority::Foreground,
        .requiredLanes = {corax::jobs::ResourceLane::Cpu},
        .cancellable = true,
    };
    const corax::jobs::JobFailure failure{
        .code = QStringLiteral("test.presentation_failure"),
        .userMessage = QStringLiteral("The visible test job failed."),
        .technicalContext = QStringLiteral("Synthetic presentation failure context."),
        .remediation = QStringLiteral("Retry the visible test job."),
        .retryable = true,
    };
    const auto failureId = scheduler.submit(std::make_unique<corax::jobs::FunctionalJob>(
        failureDescriptor,
        [failure](corax::jobs::JobContext&) { return corax::jobs::JobOutcome::failed(failure); }));
    QVERIFY(!failureId.isNull());

    const corax::jobs::JobDescriptor issueDescriptor{
        .id = QUuid{QStringLiteral("{d79d18ca-b473-425b-88bb-9d0583649538}")},
        .type = QStringLiteral("test.presentation_issues"),
        .owner = QStringLiteral("qml_smoke_test"),
        .payloadVersion = 1,
        .title = QStringLiteral("Presentation issue test"),
        .projectId = {},
        .priority = corax::jobs::JobPriority::Foreground,
        .requiredLanes = {corax::jobs::ResourceLane::Cpu},
        .cancellable = true,
    };
    const corax::jobs::JobIssue issue{
        .code = QStringLiteral("test.presentation_issue"),
        .severity = corax::jobs::JobIssueSeverity::ItemFailure,
        .userMessage = QStringLiteral("One synthetic item needs attention."),
        .technicalContext = QStringLiteral("Synthetic structured issue context."),
        .affectedObjectId = QStringLiteral("asset-test-17"),
        .retryable = true,
    };
    const auto issueId = scheduler.submit(std::make_unique<corax::jobs::FunctionalJob>(
        issueDescriptor,
        [issue](corax::jobs::JobContext&)
        { return corax::jobs::JobOutcome::succeededWithIssues({issue}); }));
    QVERIFY(!issueId.isNull());

    auto* model = jobsController.jobs();
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 2'000);
    const QModelIndex failureIndex = model->index(0, 0);
    QTRY_COMPARE_WITH_TIMEOUT(
        model->data(failureIndex, corax::presentation::JobListModel::StateRole).toString(),
        QStringLiteral("failed"),
        2'000);
    const QModelIndex issueIndex = model->index(1, 0);
    QTRY_COMPARE_WITH_TIMEOUT(
        model->data(issueIndex, corax::presentation::JobListModel::StateRole).toString(),
        QStringLiteral("succeeded_with_issues"),
        2'000);
    QCOMPARE(
        model->data(failureIndex, corax::presentation::JobListModel::ErrorMessageRole).toString(),
        failure.userMessage);
    QCOMPARE(model->data(failureIndex, corax::presentation::JobListModel::ErrorCodeRole).toString(),
             failure.code);

    const auto failureDetails = model->detailsForJob(failureId.toString(QUuid::WithoutBraces));
    const auto presentedFailure = failureDetails.value(QStringLiteral("failure")).toMap();
    QCOMPARE(presentedFailure.value(QStringLiteral("code")).toString(), failure.code);
    QCOMPARE(presentedFailure.value(QStringLiteral("userMessage")).toString(), failure.userMessage);
    QCOMPARE(presentedFailure.value(QStringLiteral("technicalContext")).toString(),
             failure.technicalContext);
    QCOMPARE(presentedFailure.value(QStringLiteral("remediation")).toString(), failure.remediation);
    QCOMPARE(presentedFailure.value(QStringLiteral("retryable")).toBool(), failure.retryable);

    const auto issueDetails = model->detailsForJob(issueId.toString(QUuid::WithoutBraces));
    const auto presentedIssues = issueDetails.value(QStringLiteral("issues")).toList();
    QCOMPARE(presentedIssues.size(), 1);
    const auto presentedIssue = presentedIssues.constFirst().toMap();
    QCOMPARE(presentedIssue.value(QStringLiteral("code")).toString(), issue.code);
    QCOMPARE(presentedIssue.value(QStringLiteral("severity")).toString(),
             QStringLiteral("item_failure"));
    QCOMPARE(presentedIssue.value(QStringLiteral("userMessage")).toString(), issue.userMessage);
    QCOMPARE(presentedIssue.value(QStringLiteral("technicalContext")).toString(),
             issue.technicalContext);
    QCOMPARE(presentedIssue.value(QStringLiteral("affectedObjectId")).toString(),
             issue.affectedObjectId);
    QCOMPARE(presentedIssue.value(QStringLiteral("retryable")).toBool(), issue.retryable);

    QTRY_VERIFY_WITH_TIMEOUT(
        findVisualItem(window->contentItem(), QStringLiteral("jobFailureMessage")) != nullptr,
        3'000);
    auto* failureLabel = findVisualItem(window->contentItem(), QStringLiteral("jobFailureMessage"));
    QVERIFY(failureLabel->property("visible").toBool());
    QCOMPARE(failureLabel->property("text").toString(),
             QStringLiteral("The visible test job failed."));

    QTRY_VERIFY_WITH_TIMEOUT(
        findVisualItem(window->contentItem(), QStringLiteral("jobIssueCount")) != nullptr, 3'000);
    auto* issueLabel = findVisualItem(window->contentItem(), QStringLiteral("jobIssueCount"));
    QVERIFY(issueLabel->property("visible").toBool());
    QCOMPARE(issueLabel->property("text").toString(), QStringLiteral("1 issue"));
}

QTEST_MAIN(QmlSmokeTest)

#include "test_qml_smoke.moc"
