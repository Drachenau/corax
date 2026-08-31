// SPDX-License-Identifier: Apache-2.0

#include <corax/application/ProjectService.h>
#include <corax/build/BuildConfiguration.h>
#include <corax/jobs/JobScheduler.h>
#include <corax/platform/ApplicationIdentity.h>
#include <corax/presentation/JobsController.h>
#include <corax/presentation/ProjectController.h>
#include <corax/storage_sqlite/ProjectManifest.h>
#include <corax/storage_sqlite/ProjectWriterLock.h>
#include <corax/storage_sqlite/SqliteProjectDatabase.h>
#include <corax/storage_sqlite/SqliteProjectStore.h>
#include <corax/ui/UiConfiguration.h>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTime>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlextensionplugin.h>

#include <chrono>
#include <utility>

Q_IMPORT_QML_PLUGIN(Corax_UiPlugin)

namespace
{

using namespace std::chrono_literals;

QObject* findObjectWithText(QObject* root, const QString& text)
{
    if (root == nullptr)
    {
        return nullptr;
    }
    if (root->property("text").toString() == text)
    {
        return root;
    }
    for (auto* child : root->children())
    {
        if (auto* match = findObjectWithText(child, text))
        {
            return match;
        }
    }
    return nullptr;
}

} // namespace

class AppIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void identityAndRuntimeVersionUseBuildConfiguration();
    void realStorageLifecycleAndIdentityMismatchReachQml();
};

void AppIntegrationTest::identityAndRuntimeVersionUseBuildConfiguration()
{
    const QString configuredVersion = QString::fromLatin1(corax::build::kApplicationVersion);

    QCOMPARE(corax::platform::ApplicationIdentity::applicationVersion(), configuredVersion);
    QCOMPARE(QCoreApplication::applicationVersion(), configuredVersion);
    QCOMPARE(corax::platform::ApplicationIdentity::displayName(), QStringLiteral("Corax"));
    QCOMPARE(corax::platform::ApplicationIdentity::organizationName(), QStringLiteral("Drachenau"));
    QCOMPARE(corax::platform::ApplicationIdentity::organizationDomain(),
             QStringLiteral("drachenau.com"));
    QCOMPARE(corax::platform::ApplicationIdentity::applicationIdentifier(),
             QStringLiteral("com.drachenau.corax"));
    QCOMPARE(QCoreApplication::applicationName(),
             corax::platform::ApplicationIdentity::displayName());
    QCOMPARE(QCoreApplication::organizationName(),
             corax::platform::ApplicationIdentity::organizationName());
    QCOMPARE(QCoreApplication::organizationDomain(),
             corax::platform::ApplicationIdentity::organizationDomain());
}

void AppIntegrationTest::realStorageLifecycleAndIdentityMismatchReachQml()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString projectPath = temporaryDirectory.filePath(QStringLiteral("AppIntegration.corax"));
    const QString manifestPath =
        QDir(projectPath).filePath(QString::fromLatin1(corax::storage_sqlite::kManifestFileName));
    const QString databasePath =
        QDir(projectPath).filePath(QString::fromLatin1(corax::storage_sqlite::kDatabaseFileName));
    const QString lockPath =
        QDir(projectPath).filePath(QString::fromLatin1(corax::storage_sqlite::kWriterLockFileName));
    const QUuid projectId{QStringLiteral("{27a32f84-d3f1-40d4-938d-236315745d83}")};
    const QUuid mismatchedId{QStringLiteral("{aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee}")};
    const QDateTime createdAt{QDate(2026, 8, 31), QTime(12, 0), QTimeZone::UTC};
    const QString expectedVersion = QString::fromLatin1(corax::build::kApplicationVersion);

    corax::storage_sqlite::SqliteProjectStore projectStore;
    corax::application::ProjectService projectService{
        projectStore, [projectId] { return projectId; }, [createdAt] { return createdAt; }};
    corax::jobs::JobScheduler jobScheduler{2};
    corax::presentation::ProjectController projectController{projectService, jobScheduler};
    corax::presentation::JobsController jobsController{jobScheduler};

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("projectController"), QVariant::fromValue(&projectController)},
        {QStringLiteral("jobsController"), QVariant::fromValue(&jobsController)},
    });
    engine.loadFromModule(QStringLiteral("Corax.Ui"), QStringLiteral("Main"));
    QVERIFY(QTest::qWaitFor([&engine] { return engine.rootObjects().size() == 1; }, 3'000));

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto* projectTitle = window->findChild<QObject*>(QStringLiteral("projectTitle"));
    QVERIFY(projectTitle != nullptr);
    QCOMPARE(projectTitle->property("text").toString(), QStringLiteral("No project"));

    QSignalSpy opened{&projectController, &corax::presentation::ProjectController::projectOpened};
    QSignalSpy closed{&projectController, &corax::presentation::ProjectController::projectClosed};

    projectController.createProject(QUrl::fromLocalFile(projectPath),
                                    QStringLiteral("Integration Project"));
    QVERIFY(QTest::qWaitFor([&projectController, &opened]
                            { return !projectController.busy() && opened.size() == 1; },
                            10'000));
    QVERIFY(projectController.hasProject());
    QCOMPARE(projectController.projectId(), projectId.toString(QUuid::WithoutBraces));
    QCOMPARE(projectTitle->property("text").toString(), QStringLiteral("Integration Project"));
    QVERIFY(QFile::exists(lockPath));
    QFile lockFile(lockPath);
    QVERIFY(lockFile.open(QIODevice::ReadOnly));
    const QJsonDocument lockDocument = QJsonDocument::fromJson(lockFile.readAll());
    QVERIFY(lockDocument.isObject());
    QCOMPARE(lockDocument.object().value(QStringLiteral("applicationVersion")).toString(),
             expectedVersion);

    projectController.closeProject();
    QVERIFY(QTest::qWaitFor([&projectController, &closed]
                            { return !projectController.busy() && closed.size() == 1; },
                            10'000));
    QVERIFY(!projectController.hasProject());
    QCOMPARE(projectTitle->property("text").toString(), QStringLiteral("No project"));
    QVERIFY(!QFile::exists(lockPath));

    corax::storage_sqlite::ManifestStore manifests;
    auto persistedManifest = manifests.read(manifestPath);
    QVERIFY2(
        persistedManifest,
        qPrintable(persistedManifest ? QString{} : persistedManifest.error().technicalContext));
    QCOMPARE(persistedManifest.value().minimumCoraxVersion, expectedVersion);

    auto database = corax::storage_sqlite::SqliteProjectDatabase::openExisting(
        databasePath, projectPath, projectId);
    QVERIFY2(database, qPrintable(database ? QString{} : database.error().technicalContext));
    auto migrationLedger = database.value()->migrationLedger();
    QVERIFY2(migrationLedger,
             qPrintable(migrationLedger ? QString{} : migrationLedger.error().technicalContext));
    QCOMPARE(migrationLedger.value().size(), 1);
    QCOMPARE(migrationLedger.value().constFirst().applicationVersion, expectedVersion);
    database.value().reset();

    projectController.openProject(QUrl::fromLocalFile(projectPath));
    QVERIFY(QTest::qWaitFor([&projectController, &opened]
                            { return !projectController.busy() && opened.size() == 2; },
                            10'000));
    QCOMPARE(projectController.projectId(), projectId.toString(QUuid::WithoutBraces));
    QCOMPARE(projectTitle->property("text").toString(), QStringLiteral("Integration Project"));

    projectController.closeProject();
    QVERIFY(QTest::qWaitFor([&projectController, &closed]
                            { return !projectController.busy() && closed.size() == 2; },
                            10'000));
    QVERIFY(!QFile::exists(lockPath));

    persistedManifest.value().projectId = mismatchedId;
    auto manifestWritten = manifests.write(manifestPath, persistedManifest.value());
    QVERIFY2(manifestWritten,
             qPrintable(manifestWritten ? QString{} : manifestWritten.error().technicalContext));

    projectController.openProject(QUrl::fromLocalFile(projectPath));
    QVERIFY(QTest::qWaitFor([&projectController]
                            { return !projectController.busy() && projectController.hasError(); },
                            10'000));
    QVERIFY(!projectController.hasProject());
    QCOMPARE(projectController.errorCode(), QStringLiteral("project.identity_mismatch"));
    QCOMPARE(projectController.errorMessage(),
             QStringLiteral("The project manifest and database do not belong together."));
    QVERIFY(projectController.errorTechnicalContext().contains(
        projectId.toString(QUuid::WithoutBraces)));
    QVERIFY(projectController.errorTechnicalContext().contains(
        mismatchedId.toString(QUuid::WithoutBraces)));
    QCOMPARE(
        projectController.errorRemediation(),
        QStringLiteral("Restore a matching manifest and database from the same project backup."));
    QCOMPARE(projectController.errorAffectedPath(), projectPath);
    QVERIFY(!projectController.errorRetryable());
    QVERIFY(!projectStore.currentProject().has_value());
    QVERIFY(!QFile::exists(lockPath));

    auto* errorBanner = window->findChild<QObject*>(QStringLiteral("errorBanner"));
    auto* errorMessage = window->findChild<QObject*>(QStringLiteral("errorMessage"));
    QVERIFY(errorBanner != nullptr);
    QVERIFY(errorMessage != nullptr);
    QVERIFY(errorBanner->property("visible").toBool());
    QCOMPARE(errorMessage->property("text").toString(), projectController.errorMessage());
    QVERIFY(findObjectWithText(window, projectController.errorCode()) != nullptr);

    bool eventProcessed = false;
    QTimer::singleShot(0, [&eventProcessed] { eventProcessed = true; });
    QVERIFY(QTest::qWaitFor([&eventProcessed] { return eventProcessed; }, 1'000));

    projectController.clearError();
    QVERIFY(QTest::qWaitFor([errorBanner] { return !errorBanner->property("visible").toBool(); },
                            1'000));
    QVERIFY(jobScheduler.waitForShutdown(3s));
}

int main(int argc, char* argv[])
{
    corax::platform::ApplicationIdentity::applyToQt();
    QGuiApplication application(argc, argv);
    corax::ui::configureQuickControls();

    AppIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_app_integration.moc"
