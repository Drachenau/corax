// SPDX-License-Identifier: Apache-2.0

#include "corax/application/ProjectService.h"
#include "corax/domain/AppError.h"
#include "corax/domain/ProjectInfo.h"
#include "corax/domain/Result.h"

#include <QTest>

#include <optional>

namespace
{

using corax::application::IProjectStore;
using corax::application::NewProject;
using corax::application::ProjectService;
using corax::domain::AppError;
using corax::domain::ErrorCode;
using corax::domain::ProjectInfo;
using corax::domain::Result;

class FakeProjectStore final : public IProjectStore
{
public:
    Result<ProjectInfo> createProject(const NewProject& project) override
    {
        ++createCalls;
        lastCreated = project;
        current = ProjectInfo{
            .projectId = project.projectId,
            .displayName = project.displayName,
            .projectPath = project.projectPath,
            .createdAtUtc = project.createdAtUtc,
            .revision = 0,
        };
        return Result<ProjectInfo>::success(current.value());
    }

    Result<ProjectInfo> openProject(const QString& projectDirectory) override
    {
        ++openCalls;
        current = ProjectInfo{
            .projectId = QUuid(QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")),
            .displayName = QStringLiteral("Opened"),
            .projectPath = projectDirectory,
            .createdAtUtc = QDateTime::fromString(QStringLiteral("2026-08-30T12:00:00.000Z"),
                                                  Qt::ISODateWithMs),
            .revision = 3,
        };
        return Result<ProjectInfo>::success(current.value());
    }

    Result<void> closeProject() override
    {
        ++closeCalls;
        current.reset();
        return Result<void>::success();
    }

    std::optional<ProjectInfo> currentProject() const override
    {
        return current;
    }

    int createCalls{0};
    int openCalls{0};
    int closeCalls{0};
    std::optional<NewProject> lastCreated;
    std::optional<ProjectInfo> current;
};

class CoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void resultCarriesValueAndStructuredError()
    {
        auto success = Result<int>::success(42);
        QVERIFY(success);
        QCOMPARE(success.value(), 42);

        const AppError expected{
            .code = ErrorCode::InvalidArgument,
            .userMessage = QStringLiteral("Safe summary"),
            .technicalContext = QStringLiteral("Detailed context"),
            .remediation = QStringLiteral("Try a different value"),
            .affectedPath = QStringLiteral("project-token"),
            .retryable = true,
        };
        auto failure = Result<int>::failure(expected);
        QVERIFY(!failure);
        QCOMPARE(failure.error().stableCode(), QStringLiteral("core.invalid_argument"));
        QCOMPARE(failure.error().userMessage, expected.userMessage);
        QCOMPARE(failure.error().technicalContext, expected.technicalContext);
        QCOMPARE(failure.error().remediation, expected.remediation);
        QCOMPARE(failure.error().affectedPath, expected.affectedPath);
        QVERIFY(failure.error().retryable);

        auto voidSuccess = Result<void>::success();
        QVERIFY(voidSuccess);
        voidSuccess.value();
        auto voidFailure = Result<void>::failure(expected);
        QVERIFY(!voidFailure);
        QCOMPARE(voidFailure.error().stableCode(), QStringLiteral("core.invalid_argument"));
    }

    void projectInfoValidatesCoreInvariants()
    {
        ProjectInfo info{
            .projectId = QUuid(QStringLiteral("11111111-2222-4333-8444-555555555555")),
            .displayName = QStringLiteral("Fixture"),
            .projectPath = QStringLiteral("/portable/project.corax"),
            .createdAtUtc = QDateTime::fromString(QStringLiteral("2026-08-30T12:00:00.000Z"),
                                                  Qt::ISODateWithMs),
            .revision = 0,
        };
        QVERIFY(info.isValid());
        info.projectId = {};
        QVERIFY(!info.isValid());
        info.projectId = QUuid(QStringLiteral("11111111-2222-4333-8444-555555555555"));
        info.revision = -1;
        QVERIFY(!info.isValid());
    }

    void projectServiceUsesInjectedIdentityAndClock()
    {
        FakeProjectStore store;
        const QUuid expectedId(QStringLiteral("01234567-89ab-4cde-8fab-0123456789ab"));
        const QDateTime expectedTime = QDateTime::fromString(
            QStringLiteral("2026-08-30T10:11:12.345-05:00"), Qt::ISODateWithMs);
        ProjectService service(
            store, [expectedId] { return expectedId; }, [expectedTime] { return expectedTime; });

        auto created = service.createProject(QStringLiteral("/tmp/fixture.corax"),
                                             QStringLiteral("  Fixture Project  "));
        QVERIFY(created);
        QCOMPARE(store.createCalls, 1);
        QVERIFY(store.lastCreated.has_value());
        QCOMPARE(store.lastCreated->projectId, expectedId);
        QCOMPARE(store.lastCreated->displayName, QStringLiteral("Fixture Project"));
        QCOMPARE(store.lastCreated->createdAtUtc, expectedTime.toUTC());
        QVERIFY(service.currentProject().has_value());

        auto closed = service.closeProject();
        QVERIFY(closed);
        QCOMPARE(store.closeCalls, 1);
        QVERIFY(!service.currentProject().has_value());

        auto opened = service.openProject(QStringLiteral("/tmp/fixture.corax"));
        QVERIFY(opened);
        QCOMPARE(store.openCalls, 1);
        QCOMPARE(opened.value().revision, 3);
    }

    void projectServiceRejectsInvalidInputBeforeStoreCall()
    {
        FakeProjectStore store;
        ProjectService service(store);

        auto missingPath = service.createProject({}, QStringLiteral("Name"));
        QVERIFY(!missingPath);
        QCOMPARE(missingPath.error().stableCode(), QStringLiteral("core.invalid_argument"));

        auto missingName = service.createProject(QStringLiteral("/tmp/fixture.corax"), "   ");
        QVERIFY(!missingName);
        QCOMPARE(missingName.error().stableCode(), QStringLiteral("core.invalid_argument"));

        auto missingOpenPath = service.openProject({});
        QVERIFY(!missingOpenPath);
        QCOMPARE(store.createCalls, 0);
        QCOMPARE(store.openCalls, 0);
    }

    void projectServiceConvertsDependencyExceptions()
    {
        FakeProjectStore store;
        ProjectService service(
            store,
            []() -> QUuid { throw std::runtime_error("fixture ID failure"); },
            [] { return QDateTime::currentDateTimeUtc(); });

        auto result = service.createProject(QStringLiteral("/tmp/fixture.corax"), "Fixture");
        QVERIFY(!result);
        QCOMPARE(result.error().stableCode(), QStringLiteral("core.internal_error"));
        QVERIFY(result.error().technicalContext.contains(QStringLiteral("fixture ID failure")));
        QCOMPARE(store.createCalls, 0);
    }
};

} // namespace

QTEST_APPLESS_MAIN(CoreTests)

#include "CoreTests.moc"
