// SPDX-License-Identifier: Apache-2.0

#include "corax/application/ProjectService.h"
#include "corax/domain/AppError.h"
#include "corax/storage_sqlite/ProjectManifest.h"
#include "corax/storage_sqlite/ProjectWriterLock.h"
#include "corax/storage_sqlite/SqliteProjectDatabase.h"
#include "corax/storage_sqlite/SqliteProjectStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>
#include <optional>

namespace
{

using corax::application::ProjectService;
using corax::domain::AppError;
using corax::domain::ErrorCode;
using corax::domain::ProjectInfo;
using corax::domain::Result;
using corax::storage_sqlite::IAtomicFileWriter;
using corax::storage_sqlite::ISqliteInitializationFaultInjector;
using corax::storage_sqlite::ManifestStore;
using corax::storage_sqlite::ProjectManifest;
using corax::storage_sqlite::SqliteProjectDatabase;
using corax::storage_sqlite::SqliteProjectStore;

constexpr auto kFixtureIdText = "12345678-90ab-4cde-8fab-1234567890ab";
constexpr auto kOtherIdText = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
constexpr auto kFixtureTimeText = "2026-08-30T15:16:17.123Z";

QDateTime fixtureTime()
{
    return QDateTime::fromString(QString::fromLatin1(kFixtureTimeText), Qt::ISODateWithMs);
}

ProjectInfo fixtureProject(const QString& path)
{
    return {
        .projectId = QUuid(QString::fromLatin1(kFixtureIdText)),
        .displayName = QStringLiteral("Storage Fixture"),
        .projectPath = path,
        .createdAtUtc = fixtureTime(),
        .revision = 0,
    };
}

ProjectManifest fixtureManifest()
{
    return {
        .projectId = QUuid(QString::fromLatin1(kFixtureIdText)),
        .displayName = QStringLiteral("Storage Fixture"),
        .databaseFile = QStringLiteral("project.sqlite3"),
        .createdAtUtc = fixtureTime(),
        .minimumCoraxVersion = QStringLiteral("0.1.0"),
        .preservedFields =
            {
                {QStringLiteral("futureMetadata"),
                 QJsonObject{
                     {QStringLiteral("kept"), true},
                 }},
            },
    };
}

class FailingAtomicWriter final : public IAtomicFileWriter
{
public:
    Result<void> writeAtomically(const QString& path, const QByteArray&) override
    {
        ++calls;
        return Result<void>::failure({
            .code = ErrorCode::ManifestWriteFailed,
            .userMessage = QStringLiteral("Injected atomic write failure."),
            .technicalContext = QStringLiteral("The test writer rejected the commit."),
            .remediation = QStringLiteral("Use the real writer."),
            .affectedPath = path,
            .retryable = true,
        });
    }

    int calls{0};
};

class FailBeforeMigrationCommit final : public ISqliteInitializationFaultInjector
{
public:
    Result<void> beforeInitialMigrationCommit() override
    {
        ++calls;
        return Result<void>::failure({
            .code = ErrorCode::TransactionFailed,
            .userMessage = QStringLiteral("Injected transaction failure."),
            .technicalContext = QStringLiteral("Failure before initial migration commit."),
            .remediation = QStringLiteral("Retry without the injected failure."),
            .affectedPath = {},
            .retryable = true,
        });
    }

    int calls{0};
};

class StorageTests final : public QObject
{
    Q_OBJECT

private slots:
    void manifestRoundTripPreservesUnknownFields()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString path = temporaryDirectory.filePath(QStringLiteral("corax.project.json"));
        ManifestStore store;

        auto written = store.write(path, fixtureManifest());
        QVERIFY2(written, qPrintable(written ? QString{} : written.error().technicalContext));
        auto read = store.read(path);
        QVERIFY2(read, qPrintable(read ? QString{} : read.error().technicalContext));
        QCOMPARE(read.value().projectId, QUuid(QString::fromLatin1(kFixtureIdText)));
        QCOMPARE(read.value().displayName, QStringLiteral("Storage Fixture"));
        QCOMPARE(read.value().databaseFile, QStringLiteral("project.sqlite3"));
        QCOMPARE(read.value().createdAtUtc, fixtureTime());
        QVERIFY(read.value().preservedFields.contains(QStringLiteral("futureMetadata")));
        QVERIFY(read.value()
                    .preservedFields.value(QStringLiteral("futureMetadata"))
                    .toObject()
                    .value(QStringLiteral("kept"))
                    .toBool());
        QVERIFY(read.value().preservedFields.value(QStringLiteral("sourceRoots")).isArray());
        QVERIFY(
            read.value().preservedFields.value(QStringLiteral("sourceRoots")).toArray().isEmpty());

        QFile raw(path);
        QVERIFY(raw.open(QIODevice::ReadOnly));
        const QByteArray bytes = raw.readAll();
        QVERIFY(!bytes.startsWith("\xEF\xBB\xBF"));
        QVERIFY(bytes.endsWith('\n'));
    }

    void manifestRejectsInvalidForms_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<QString>("stableCode");

        QTest::newRow("invalid-json")
            << QByteArray("{not-json") << QStringLiteral("manifest.invalid");
        QTest::newRow("byte-order-mark")
            << QByteArray("\xEF\xBB\xBF{\"format\":\"org.corax.project\"}")
            << QStringLiteral("manifest.invalid");
        QTest::newRow("future-version") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":2,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"0.1.0"
            })json") << QStringLiteral("manifest.version_unsupported");
        QTest::newRow("wrong-format") << QByteArray(R"json({
                "format":"example.other","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"0.1.0"
            })json") << QStringLiteral("manifest.invalid");
        QTest::newRow("future-minimum-version") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"9.0.0"
            })json") << QStringLiteral("manifest.version_unsupported");
        QTest::newRow("invalid-minimum-version") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"v0.1"
            })json") << QStringLiteral("manifest.invalid");
        QTest::newRow("noncanonical-id") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"{12345678-90ab-4cde-8fab-1234567890ab}",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"0.1.0"
            })json") << QStringLiteral("manifest.invalid");
        QTest::newRow("database-traversal") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"../outside.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"0.1.0"
            })json") << QStringLiteral("manifest.invalid");
        QTest::newRow("local-created-at") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123","minimumCoraxVersion":"0.1.0"
            })json") << QStringLiteral("manifest.invalid");
        QTest::newRow("source-roots-not-array") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"0.1.0",
                "sourceRoots":{}
            })json") << QStringLiteral("manifest.invalid");
        QTest::newRow("source-roots-not-yet-supported") << QByteArray(R"json({
                "format":"org.corax.project","formatVersion":1,
                "projectId":"12345678-90ab-4cde-8fab-1234567890ab",
                "displayName":"Fixture","database":"project.sqlite3",
                "createdAt":"2026-08-30T15:16:17.123Z","minimumCoraxVersion":"0.1.0",
                "sourceRoots":[{"id":"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"}]
            })json") << QStringLiteral("manifest.feature_unsupported");
    }

    void manifestRejectsInvalidForms()
    {
        QFETCH(QByteArray, contents);
        QFETCH(QString, stableCode);
        ManifestStore store;
        auto result = store.parse(contents, QStringLiteral("fixture-manifest"));
        QVERIFY(!result);
        QCOMPARE(result.error().stableCode(), stableCode);
        QVERIFY(!result.error().userMessage.isEmpty());
        QVERIFY(!result.error().technicalContext.isEmpty());
        QVERIFY(!result.error().remediation.isEmpty());
    }

    void manifestAtomicWriteFailureIsVisibleAndDoesNotCreateFinalFile()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString path = temporaryDirectory.filePath(QStringLiteral("corax.project.json"));
        auto writer = std::make_shared<FailingAtomicWriter>();
        ManifestStore store(writer);

        auto result = store.write(path, fixtureManifest());
        QVERIFY(!result);
        QCOMPARE(result.error().stableCode(), QStringLiteral("manifest.write_failed"));
        QCOMPARE(writer->calls, 1);
        QVERIFY(!QFile::exists(path));
    }

    void manifestWriteRejectsUnsupportedPreservedSourceRoots()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString path = temporaryDirectory.filePath(QStringLiteral("corax.project.json"));
        ProjectManifest manifest = fixtureManifest();
        manifest.preservedFields.insert(
            QStringLiteral("sourceRoots"),
            QJsonArray{QJsonObject{
                {QStringLiteral("id"), QString::fromLatin1(kOtherIdText)},
            }});
        ManifestStore store;

        auto result = store.write(path, manifest);
        QVERIFY(!result);
        QCOMPARE(result.error().stableCode(), QStringLiteral("manifest.invalid"));
        QVERIFY(!QFile::exists(path));
    }

    void sqliteInitializesReopensAndVerifiesFeatures()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("project.sqlite3"));
        const ProjectInfo project = fixtureProject(temporaryDirectory.path());

        auto initialized = SqliteProjectDatabase::initializeNew(databasePath, project);
        QVERIFY2(initialized,
                 qPrintable(initialized ? QString{} : initialized.error().technicalContext));
        auto info = initialized.value()->projectInfo();
        QVERIFY(info);
        QCOMPARE(info.value(), project);

        const auto runtime = initialized.value()->runtimeInfo();
        QCOMPARE(runtime.version, QStringLiteral("3.53.4"));
        QVERIFY(runtime.fts5Available);
        QVERIFY(runtime.threadSafe);
        QVERIFY(runtime.compileOptions.contains(QStringLiteral("ENABLE_FTS5")));
        QVERIFY(runtime.compileOptions.contains(QStringLiteral("THREADSAFE=1")));
        QVERIFY(runtime.compileOptions.contains(QStringLiteral("DQS=0")));
        QVERIFY(runtime.compileOptions.contains(QStringLiteral("OMIT_LOAD_EXTENSION")));
        QVERIFY(runtime.compileOptions.contains(QStringLiteral("DEFAULT_FOREIGN_KEYS")));

        auto ledger = initialized.value()->migrationLedger();
        QVERIFY(ledger);
        QCOMPARE(ledger.value().size(), 1);
        QCOMPARE(ledger.value().front().sequence, 1);
        QCOMPARE(ledger.value().front().migrationId, SqliteProjectDatabase::initialMigrationId());
        QCOMPARE(ledger.value().front().checksum,
                 SqliteProjectDatabase::initialMigrationChecksum());
        QCOMPARE(ledger.value().front().applicationVersion, QStringLiteral("0.1.0"));
        QCOMPARE(ledger.value().front().result, QStringLiteral("applied"));
        QVERIFY(ledger.value().front().startedAtUtc.isValid());
        QVERIFY(ledger.value().front().completedAtUtc.isValid());

        initialized.value().reset();
        auto reopened = SqliteProjectDatabase::openExisting(
            databasePath, temporaryDirectory.path(), project.projectId);
        QVERIFY2(reopened, qPrintable(reopened ? QString{} : reopened.error().technicalContext));
        auto reopenedInfo = reopened.value()->projectInfo();
        QVERIFY(reopenedInfo);
        QCOMPARE(reopenedInfo.value(), project);
    }

    void sqliteInitializationTransactionRollsBackOnInjectedFailure()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("project.sqlite3"));
        const ProjectInfo project = fixtureProject(temporaryDirectory.path());
        FailBeforeMigrationCommit injector;

        auto failed = SqliteProjectDatabase::initializeNew(databasePath, project, &injector);
        QVERIFY(!failed);
        QCOMPARE(failed.error().stableCode(), QStringLiteral("database.transaction_failed"));
        QCOMPARE(injector.calls, 1);

        // A second initialization succeeds against the same file only if the
        // schema, metadata, ledger, application ID, and user version rolled back.
        auto retried = SqliteProjectDatabase::initializeNew(databasePath, project);
        QVERIFY2(retried, qPrintable(retried ? QString{} : retried.error().technicalContext));
        auto ledger = retried.value()->migrationLedger();
        QVERIFY(ledger);
        QCOMPARE(ledger.value().size(), 1);
        QCOMPARE(ledger.value().front().checksum,
                 SqliteProjectDatabase::initialMigrationChecksum());
    }

    void invalidMigrationLedgerOpenDoesNotMutateDatabase()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString databasePath = temporaryDirectory.filePath(QStringLiteral("project.sqlite3"));
        const ProjectInfo project = fixtureProject(temporaryDirectory.path());

        auto initialized = SqliteProjectDatabase::initializeNew(databasePath, project);
        QVERIFY2(initialized,
                 qPrintable(initialized ? QString{} : initialized.error().technicalContext));
        initialized.value().reset();

        QFile fixture(databasePath);
        QVERIFY(fixture.open(QIODevice::ReadWrite));
        QByteArray bytes = fixture.readAll();
        QVERIFY(bytes.size() > 100);
        QCOMPARE(bytes.first(16), QByteArray("SQLite format 3\0", 16));
        const QByteArray migrationId = SqliteProjectDatabase::initialMigrationId().toUtf8();
        const qsizetype migrationOffset = bytes.indexOf(migrationId);
        QVERIFY(migrationOffset >= 0);

        // Select rollback-journal mode in the database header and corrupt only
        // the semantic migration ID. SQLite quick_check still accepts the file.
        bytes[18] = '\x01';
        bytes[19] = '\x01';
        bytes[migrationOffset] = '9';
        QVERIFY(fixture.resize(0));
        QVERIFY(fixture.seek(0));
        QCOMPARE(fixture.write(bytes), qint64(bytes.size()));
        fixture.close();

        auto rejected = SqliteProjectDatabase::openExisting(
            databasePath, temporaryDirectory.path(), project.projectId);
        QVERIFY(!rejected);
        QCOMPARE(rejected.error().stableCode(), QStringLiteral("database.migration_failed"));

        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray afterRejectedOpen = fixture.readAll();
        fixture.close();
        QCOMPARE(afterRejectedOpen, bytes);
        QVERIFY(!QFile::exists(databasePath + QStringLiteral("-wal")));
        QVERIFY(!QFile::exists(databasePath + QStringLiteral("-shm")));
    }

    void projectCreateCloseReopenLifecycle()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("Lifecycle.corax"));
        const QUuid expectedId(QString::fromLatin1(kFixtureIdText));
        const QDateTime expectedTime = fixtureTime();
        SqliteProjectStore store;
        ProjectService service(
            store, [expectedId] { return expectedId; }, [expectedTime] { return expectedTime; });

        auto created = service.createProject(projectPath, QStringLiteral("Lifecycle Fixture"));
        QVERIFY2(created, qPrintable(created ? QString{} : created.error().technicalContext));
        QCOMPARE(created.value().projectId, expectedId);
        QCOMPARE(created.value().displayName, QStringLiteral("Lifecycle Fixture"));
        QCOMPARE(created.value().revision, 0);
        QVERIFY(QFile::exists(QDir(projectPath).filePath(QStringLiteral("corax.project.json"))));
        QVERIFY(QFile::exists(QDir(projectPath).filePath(QStringLiteral("project.sqlite3"))));
        QVERIFY(QFile::exists(QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock"))));
        QVERIFY(QDir(QDir(projectPath).filePath(QStringLiteral("managed/originals"))).exists());
        QVERIFY(QDir(QDir(projectPath).filePath(QStringLiteral("annotations/masks"))).exists());
        QVERIFY(QDir(QDir(projectPath).filePath(QStringLiteral("reports/exports"))).exists());
        QVERIFY(QDir(QDir(projectPath).filePath(QStringLiteral("cache/thumbnails"))).exists());

        auto closed = service.closeProject();
        QVERIFY(closed);
        QVERIFY(!QFile::exists(QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock"))));
        QVERIFY(!service.currentProject().has_value());

        auto reopened = service.openProject(projectPath);
        QVERIFY2(reopened, qPrintable(reopened ? QString{} : reopened.error().technicalContext));
        QCOMPARE(reopened.value().projectId, expectedId);
        QCOMPARE(reopened.value().displayName, QStringLiteral("Lifecycle Fixture"));
        QCOMPARE(reopened.value().createdAtUtc, expectedTime);
        QVERIFY(service.closeProject());
    }

    void existingEmptyDirectoryIsAcceptedAndNonemptyDirectoryIsRejected()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        QDir parent(temporaryDirectory.path());
        QVERIFY(parent.mkdir(QStringLiteral("Empty.corax")));
        const QString emptyPath = parent.filePath(QStringLiteral("Empty.corax"));

        SqliteProjectStore store;
        ProjectService service(
            store,
            [] { return QUuid(QString::fromLatin1(kFixtureIdText)); },
            [] { return fixtureTime(); });
        auto accepted = service.createProject(emptyPath, QStringLiteral("Existing Empty"));
        QVERIFY2(accepted, qPrintable(accepted ? QString{} : accepted.error().technicalContext));
        QVERIFY(service.closeProject());

        QVERIFY(parent.mkdir(QStringLiteral("Nonempty.corax")));
        QFile marker(parent.filePath(QStringLiteral("Nonempty.corax/keep.txt")));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        QCOMPARE(marker.write("keep"), qint64(4));
        marker.close();
        auto rejected = service.createProject(parent.filePath(QStringLiteral("Nonempty.corax")),
                                              QStringLiteral("Must Not Overwrite"));
        QVERIFY(!rejected);
        QCOMPARE(rejected.error().stableCode(), QStringLiteral("project.already_exists"));
        QVERIFY(QFile::exists(marker.fileName()));
    }

    void simultaneousCreateKeepsWinnerArtifacts()
    {
        struct Outcome final
        {
            std::optional<Result<ProjectInfo>> created;
            std::optional<Result<void>> closed;
        };

        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("Concurrent.corax"));
        QSemaphore ready;
        QSemaphore start;
        QSemaphore missingPathObserved;
        QSemaphore continueCreation;
        QSemaphore completed;
        QSemaphore releaseWinner;
        Outcome firstOutcome;
        Outcome secondOutcome;

        const auto createWorker =
            [&](const QString& idText, const QString& displayName, Outcome& outcome)
        {
            SqliteProjectStore store({},
                                     [&]
                                     {
                                         missingPathObserved.release();
                                         continueCreation.acquire();
                                     });
            ProjectService service(
                store, [idText] { return QUuid(idText); }, [] { return fixtureTime(); });
            ready.release();
            start.acquire();
            outcome.created.emplace(service.createProject(projectPath, displayName));
            completed.release();
            releaseWinner.acquire();
            if (outcome.created.has_value() && outcome.created->hasValue())
            {
                outcome.closed.emplace(service.closeProject());
            }
        };

        auto firstThread = std::unique_ptr<QThread>(QThread::create(
            [&]
            {
                createWorker(
                    QString::fromLatin1(kFixtureIdText), QStringLiteral("First"), firstOutcome);
            }));
        auto secondThread = std::unique_ptr<QThread>(QThread::create(
            [&]
            {
                createWorker(
                    QString::fromLatin1(kOtherIdText), QStringLiteral("Second"), secondOutcome);
            }));
        firstThread->start();
        secondThread->start();

        const bool bothReady = ready.tryAcquire(2, 5'000);
        start.release(2);
        const bool bothObservedMissingPath = missingPathObserved.tryAcquire(2, 5'000);
        continueCreation.release(2);
        const bool bothCompleted = completed.tryAcquire(2, 10'000);

        bool manifestExists = false;
        bool databaseExists = false;
        bool lockExists = false;
        bool requiredDirectoriesExist = false;
        if (bothCompleted)
        {
            manifestExists =
                QFile::exists(QDir(projectPath).filePath(QStringLiteral("corax.project.json")));
            databaseExists =
                QFile::exists(QDir(projectPath).filePath(QStringLiteral("project.sqlite3")));
            lockExists =
                QFile::exists(QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock")));
            requiredDirectoriesExist =
                QDir(QDir(projectPath).filePath(QStringLiteral("managed/originals"))).exists() &&
                QDir(QDir(projectPath).filePath(QStringLiteral("annotations/masks"))).exists() &&
                QDir(QDir(projectPath).filePath(QStringLiteral("cache/thumbnails"))).exists();
        }

        releaseWinner.release(2);
        const bool firstFinished = firstThread->wait(10'000);
        const bool secondFinished = secondThread->wait(10'000);
        const bool outcomesAvailable =
            firstOutcome.created.has_value() && secondOutcome.created.has_value();

        QVERIFY(bothReady);
        QVERIFY(bothObservedMissingPath);
        QVERIFY(bothCompleted);
        QVERIFY(firstFinished);
        QVERIFY(secondFinished);
        QVERIFY(outcomesAvailable);
        if (!outcomesAvailable)
        {
            return;
        }

        const bool firstSucceeded = firstOutcome.created->hasValue();
        const bool secondSucceeded = secondOutcome.created->hasValue();
        const int successCount =
            static_cast<int>(firstSucceeded) + static_cast<int>(secondSucceeded);
        QCOMPARE(successCount, 1);
        QVERIFY(manifestExists);
        QVERIFY(databaseExists);
        QVERIFY(lockExists);
        QVERIFY(requiredDirectoriesExist);
        QVERIFY(!QFile::exists(QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock"))));

        const Result<ProjectInfo>& rejected =
            firstSucceeded ? secondOutcome.created.value() : firstOutcome.created.value();
        QVERIFY(!rejected.hasValue());
        QCOMPARE(rejected.error().stableCode(), QStringLiteral("project.already_exists"));

        const std::optional<Result<void>>& winnerClosed =
            firstSucceeded ? firstOutcome.closed : secondOutcome.closed;
        QVERIFY(winnerClosed.has_value());
        if (!winnerClosed.has_value())
        {
            return;
        }
        QVERIFY(winnerClosed->hasValue());
    }

    void existingEmptyDirectoryRacePreservesCompletedProject()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        QDir parent(temporaryDirectory.path());
        QVERIFY(parent.mkdir(QStringLiteral("ExistingRace.corax")));
        const QString projectPath = parent.filePath(QStringLiteral("ExistingRace.corax"));

        SqliteProjectStore winnerStore;
        ProjectService winner(
            winnerStore,
            [] { return QUuid(QString::fromLatin1(kFixtureIdText)); },
            [] { return fixtureTime(); });
        std::optional<Result<ProjectInfo>> winnerCreated;
        std::optional<Result<void>> winnerClosed;

        SqliteProjectStore loserStore({},
                                      [&]
                                      {
                                          winnerCreated.emplace(winner.createProject(
                                              projectPath, QStringLiteral("Race Winner")));
                                          if (winnerCreated->hasValue())
                                          {
                                              winnerClosed.emplace(winner.closeProject());
                                          }
                                      });
        ProjectService loser(
            loserStore,
            [] { return QUuid(QString::fromLatin1(kOtherIdText)); },
            [] { return fixtureTime(); });

        auto rejected = loser.createProject(projectPath, QStringLiteral("Race Loser"));

        QVERIFY(winnerCreated.has_value());
        QVERIFY2(winnerCreated->hasValue(),
                 qPrintable(winnerCreated->hasValue() ? QString{}
                                                      : winnerCreated->error().technicalContext));
        QVERIFY(winnerClosed.has_value());
        QVERIFY(winnerClosed->hasValue());
        QVERIFY(!rejected);
        QCOMPARE(rejected.error().stableCode(), QStringLiteral("project.already_exists"));

        const QDir projectDirectory(projectPath);
        QVERIFY(QFile::exists(projectDirectory.filePath(QStringLiteral("corax.project.json"))));
        QVERIFY(QFile::exists(projectDirectory.filePath(QStringLiteral("project.sqlite3"))));
        QVERIFY(!QFile::exists(projectDirectory.filePath(QStringLiteral(".corax.writer.lock"))));
        QVERIFY(QDir(projectDirectory.filePath(QStringLiteral("managed/originals"))).exists());

        SqliteProjectStore verifierStore;
        ProjectService verifier(verifierStore);
        auto reopened = verifier.openProject(projectPath);
        QVERIFY2(reopened, qPrintable(reopened ? QString{} : reopened.error().technicalContext));
        QCOMPARE(reopened.value().projectId, QUuid(QString::fromLatin1(kFixtureIdText)));
        QCOMPARE(reopened.value().displayName, QStringLiteral("Race Winner"));
        QVERIFY(verifier.closeProject());
    }

    void manifestAndDatabaseIdentityMismatchIsRejected()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("Mismatch.corax"));
        SqliteProjectStore store;
        ProjectService service(
            store,
            [] { return QUuid(QString::fromLatin1(kFixtureIdText)); },
            [] { return fixtureTime(); });

        QVERIFY(service.createProject(projectPath, QStringLiteral("Mismatch Fixture")));
        QVERIFY(service.closeProject());

        const QString manifestPath =
            QDir(projectPath).filePath(QStringLiteral("corax.project.json"));
        ManifestStore manifests;
        auto manifest = manifests.read(manifestPath);
        QVERIFY(manifest);
        manifest.value().projectId = QUuid(QString::fromLatin1(kOtherIdText));
        QVERIFY(manifests.write(manifestPath, manifest.value()));

        const QString databasePath = QDir(projectPath).filePath(QStringLiteral("project.sqlite3"));
        QFile databaseFile(databasePath);
        QVERIFY(databaseFile.open(QIODevice::ReadWrite));
        QByteArray databaseBefore = databaseFile.readAll();
        QVERIFY(databaseBefore.size() > 100);
        databaseBefore[18] = '\x01';
        databaseBefore[19] = '\x01';
        QVERIFY(databaseFile.resize(0));
        QVERIFY(databaseFile.seek(0));
        QCOMPARE(databaseFile.write(databaseBefore), qint64(databaseBefore.size()));
        databaseFile.close();

        auto opened = service.openProject(projectPath);
        QVERIFY(!opened);
        QCOMPARE(opened.error().stableCode(), QStringLiteral("project.identity_mismatch"));
        QVERIFY(opened.error().technicalContext.contains(QString::fromLatin1(kFixtureIdText)));
        QVERIFY(opened.error().technicalContext.contains(QString::fromLatin1(kOtherIdText)));
        QVERIFY(!QFile::exists(QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock"))));
        QVERIFY(databaseFile.open(QIODevice::ReadOnly));
        QCOMPARE(databaseFile.readAll(), databaseBefore);
        databaseFile.close();
        QVERIFY(!QFile::exists(databasePath + QStringLiteral("-wal")));
        QVERIFY(!QFile::exists(databasePath + QStringLiteral("-shm")));
    }

    void secondWriterIsRejectedUntilFirstWriterCloses()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("Locked.corax"));
        SqliteProjectStore firstStore;
        ProjectService first(
            firstStore,
            [] { return QUuid(QString::fromLatin1(kFixtureIdText)); },
            [] { return fixtureTime(); });
        QVERIFY(first.createProject(projectPath, QStringLiteral("Writer One")));

        SqliteProjectStore secondStore;
        ProjectService second(secondStore);
        auto contention = second.openProject(projectPath);
        QVERIFY(!contention);
        QCOMPARE(contention.error().stableCode(), QStringLiteral("project.locked"));
        QVERIFY(contention.error().retryable);

        QVERIFY(first.closeProject());
        auto acquired = second.openProject(projectPath);
        QVERIFY2(acquired, qPrintable(acquired ? QString{} : acquired.error().technicalContext));
        QVERIFY(second.closeProject());
    }

    void staleLockIsConservativelyPreserved()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("Stale.corax"));
        SqliteProjectStore creatorStore;
        ProjectService creator(
            creatorStore,
            [] { return QUuid(QString::fromLatin1(kFixtureIdText)); },
            [] { return fixtureTime(); });
        QVERIFY(creator.createProject(projectPath, QStringLiteral("Stale Fixture")));
        QVERIFY(creator.closeProject());

        const QString lockPath = QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock"));
        const QJsonObject staleLock{
            {QStringLiteral("format"), QStringLiteral("org.corax.writer-lock")},
            {QStringLiteral("formatVersion"), 1},
            {QStringLiteral("projectId"), QString::fromLatin1(kFixtureIdText)},
            {QStringLiteral("processId"), -1},
            {QStringLiteral("host"), QStringLiteral("old-host")},
            {QStringLiteral("applicationVersion"), QStringLiteral("0.1.0")},
            {QStringLiteral("startedAt"), QStringLiteral("2000-01-01T00:00:00.000Z")},
            {QStringLiteral("ownershipToken"), QStringLiteral("stale-token")},
        };
        QFile lockFile(lockPath);
        QVERIFY(lockFile.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        const QByteArray contents = QJsonDocument(staleLock).toJson(QJsonDocument::Compact);
        QCOMPARE(lockFile.write(contents), qint64(contents.size()));
        lockFile.close();

        SqliteProjectStore openerStore;
        ProjectService opener(openerStore);
        auto opened = opener.openProject(projectPath);
        QVERIFY(!opened);
        QCOMPARE(opened.error().stableCode(), QStringLiteral("project.locked"));
        QVERIFY(opened.error().technicalContext.contains(QStringLiteral("old-host")));
        QVERIFY(QFile::exists(lockPath));
        QVERIFY(QFile::remove(lockPath));
    }

    void malformedLockIsAlsoConservativelyPreserved()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const QString projectPath =
            temporaryDirectory.filePath(QStringLiteral("MalformedLock.corax"));
        SqliteProjectStore creatorStore;
        ProjectService creator(
            creatorStore,
            [] { return QUuid(QString::fromLatin1(kFixtureIdText)); },
            [] { return fixtureTime(); });
        QVERIFY(creator.createProject(projectPath, QStringLiteral("Malformed Lock Fixture")));
        QVERIFY(creator.closeProject());

        const QString lockPath = QDir(projectPath).filePath(QStringLiteral(".corax.writer.lock"));
        QFile lockFile(lockPath);
        QVERIFY(lockFile.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(lockFile.write("not-json"), qint64(8));
        lockFile.close();

        SqliteProjectStore openerStore;
        ProjectService opener(openerStore);
        auto opened = opener.openProject(projectPath);
        QVERIFY(!opened);
        QCOMPARE(opened.error().stableCode(), QStringLiteral("project.locked"));
        QVERIFY(opened.error().technicalContext.contains(QStringLiteral("malformed")));
        QVERIFY(QFile::exists(lockPath));
        QVERIFY(QFile::remove(lockPath));
    }
};

} // namespace

QTEST_APPLESS_MAIN(StorageTests)

#include "StorageTests.moc"
