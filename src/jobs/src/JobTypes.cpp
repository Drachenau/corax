#include <corax/jobs/JobTypes.h>

#include <utility>

namespace corax::jobs
{

JobOutcome JobOutcome::succeeded()
{
    return {};
}

JobOutcome JobOutcome::succeededWithIssues(QVector<JobIssue> issues)
{
    JobOutcome outcome;
    outcome.kind = Kind::SucceededWithIssues;
    outcome.issues = std::move(issues);
    return outcome;
}

JobOutcome JobOutcome::failed(JobFailure failure)
{
    JobOutcome outcome;
    outcome.kind = Kind::Failed;
    outcome.failure = std::move(failure);
    return outcome;
}

JobOutcome JobOutcome::canceled()
{
    JobOutcome outcome;
    outcome.kind = Kind::Canceled;
    return outcome;
}

bool isTerminal(const JobState state) noexcept
{
    switch (state)
    {
    case JobState::Succeeded:
    case JobState::SucceededWithIssues:
    case JobState::Failed:
    case JobState::Canceled:
        return true;
    case JobState::Queued:
    case JobState::Running:
    case JobState::CancelRequested:
        return false;
    }
    return false;
}

QString jobStateCode(const JobState state)
{
    switch (state)
    {
    case JobState::Queued:
        return QStringLiteral("queued");
    case JobState::Running:
        return QStringLiteral("running");
    case JobState::Succeeded:
        return QStringLiteral("succeeded");
    case JobState::SucceededWithIssues:
        return QStringLiteral("succeeded_with_issues");
    case JobState::Failed:
        return QStringLiteral("failed");
    case JobState::CancelRequested:
        return QStringLiteral("cancel_requested");
    case JobState::Canceled:
        return QStringLiteral("canceled");
    }
    return QStringLiteral("unknown");
}

} // namespace corax::jobs
