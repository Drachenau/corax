#pragma once

#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

namespace corax::jobs
{

enum class JobState
{
    Queued,
    Running,
    Succeeded,
    SucceededWithIssues,
    Failed,
    CancelRequested,
    Canceled,
};

enum class JobPriority
{
    UserBlocking = 0,
    ActiveAsset = 1,
    VisiblePage = 2,
    Foreground = 3,
    Background = 4,
    Maintenance = 5,
};

enum class ResourceLane
{
    Interactive,
    Cpu,
    Io,
    DatabaseRead,
    DatabaseWrite,
    ExternalProcess,
};

enum class JobIssueSeverity
{
    Info,
    Warning,
    ItemFailure,
    Fatal,
};

struct JobIssue
{
    QString code;
    JobIssueSeverity severity{JobIssueSeverity::Warning};
    QString userMessage;
    QString technicalContext;
    QString affectedObjectId;
    bool retryable{false};
};

struct JobProgress
{
    QString phaseId;
    QString phaseLabel;
    qint64 completedUnits{0};
    qint64 totalUnits{-1};
    QString currentItem;
    int issueCount{0};
};

struct JobDescriptor
{
    QUuid id;
    QString type;
    QString owner;
    int payloadVersion{1};
    QString title;
    QString projectId;
    JobPriority priority{JobPriority::Background};
    QVector<ResourceLane> requiredLanes;
    bool cancellable{true};
};

struct JobFailure
{
    QString code;
    QString userMessage;
    QString technicalContext;
    QString remediation;
    bool retryable{false};
};

struct JobSnapshot
{
    JobDescriptor descriptor;
    JobState state{JobState::Queued};
    JobProgress progress;
    QVector<JobIssue> issues;
    JobFailure failure;
};

struct JobOutcome
{
    enum class Kind
    {
        Succeeded,
        SucceededWithIssues,
        Failed,
        Canceled,
    };

    Kind kind{Kind::Succeeded};
    QVector<JobIssue> issues;
    JobFailure failure;

    [[nodiscard]] static JobOutcome succeeded();
    [[nodiscard]] static JobOutcome succeededWithIssues(QVector<JobIssue> issues);
    [[nodiscard]] static JobOutcome failed(JobFailure failure);
    [[nodiscard]] static JobOutcome canceled();
};

[[nodiscard]] bool isTerminal(JobState state) noexcept;
[[nodiscard]] QString jobStateCode(JobState state);

} // namespace corax::jobs

Q_DECLARE_METATYPE(corax::jobs::JobState)
Q_DECLARE_METATYPE(corax::jobs::JobPriority)
Q_DECLARE_METATYPE(corax::jobs::ResourceLane)
Q_DECLARE_METATYPE(corax::jobs::JobProgress)
Q_DECLARE_METATYPE(corax::jobs::JobSnapshot)
