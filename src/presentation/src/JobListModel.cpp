#include <corax/presentation/JobListModel.h>

#include <QVariantList>

#include <algorithm>

namespace corax::presentation
{
namespace
{

QString issueSeverityCode(const jobs::JobIssueSeverity severity)
{
    switch (severity)
    {
    case jobs::JobIssueSeverity::Info:
        return QStringLiteral("info");
    case jobs::JobIssueSeverity::Warning:
        return QStringLiteral("warning");
    case jobs::JobIssueSeverity::ItemFailure:
        return QStringLiteral("item_failure");
    case jobs::JobIssueSeverity::Fatal:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

QVariantMap serializeIssue(const jobs::JobIssue& issue)
{
    return {
        {QStringLiteral("code"), issue.code},
        {QStringLiteral("severity"), issueSeverityCode(issue.severity)},
        {QStringLiteral("userMessage"), issue.userMessage},
        {QStringLiteral("technicalContext"), issue.technicalContext},
        {QStringLiteral("affectedObjectId"), issue.affectedObjectId},
        {QStringLiteral("retryable"), issue.retryable},
    };
}

QVariantList serializeIssues(const QVector<jobs::JobIssue>& issues)
{
    QVariantList serialized;
    serialized.reserve(issues.size());
    for (const auto& issue : issues)
    {
        serialized.append(serializeIssue(issue));
    }
    return serialized;
}

QVariantMap serializeFailure(const jobs::JobFailure& failure)
{
    return {
        {QStringLiteral("code"), failure.code},
        {QStringLiteral("userMessage"), failure.userMessage},
        {QStringLiteral("technicalContext"), failure.technicalContext},
        {QStringLiteral("remediation"), failure.remediation},
        {QStringLiteral("retryable"), failure.retryable},
    };
}

} // namespace

JobListModel::JobListModel(jobs::JobScheduler& scheduler, QObject* parent)
    : QAbstractListModel(parent), scheduler_(scheduler), jobs_(scheduler.snapshots())
{
    recalculateActiveCount();
    connect(&scheduler_, &jobs::JobScheduler::jobAdded, this, &JobListModel::addJob);
    connect(&scheduler_, &jobs::JobScheduler::jobUpdated, this, &JobListModel::updateJob);
    connect(&scheduler_, &jobs::JobScheduler::jobRemoved, this, &JobListModel::removeJob);
}

int JobListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(jobs_.size());
}

QVariant JobListModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= jobs_.size())
    {
        return {};
    }

    const auto& job = jobs_.at(index.row());
    switch (role)
    {
    case JobIdRole:
        return job.descriptor.id.toString(QUuid::WithoutBraces);
    case TitleRole:
        return job.descriptor.title;
    case StateRole:
        return jobs::jobStateCode(job.state);
    case StateLabelRole:
        return localizedState(job.state);
    case PhaseLabelRole:
        return job.progress.phaseLabel;
    case CompletedUnitsRole:
        return job.progress.completedUnits;
    case TotalUnitsRole:
        return job.progress.totalUnits;
    case ProgressRole:
        if (job.progress.totalUnits > 0)
        {
            return std::clamp(static_cast<double>(job.progress.completedUnits) /
                                  static_cast<double>(job.progress.totalUnits),
                              0.0,
                              1.0);
        }
        return job.state == jobs::JobState::Succeeded ||
                       job.state == jobs::JobState::SucceededWithIssues
                   ? 1.0
                   : 0.0;
    case IndeterminateRole:
        return job.progress.totalUnits < 0;
    case CanCancelRole:
        return job.descriptor.cancellable &&
               (job.state == jobs::JobState::Queued || job.state == jobs::JobState::Running);
    case IssueCountRole:
        return job.progress.issueCount;
    case ErrorMessageRole:
        return job.failure.userMessage;
    case ErrorCodeRole:
        return job.failure.code;
    case ErrorTechnicalContextRole:
        return job.failure.technicalContext;
    case ErrorRemediationRole:
        return job.failure.remediation;
    case ErrorRetryableRole:
        return job.failure.retryable;
    case IssuesRole:
        return serializeIssues(job.issues);
    default:
        return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const
{
    return {
        {JobIdRole, "jobId"},
        {TitleRole, "title"},
        {StateRole, "state"},
        {StateLabelRole, "stateLabel"},
        {PhaseLabelRole, "phaseLabel"},
        {CompletedUnitsRole, "completedUnits"},
        {TotalUnitsRole, "totalUnits"},
        {ProgressRole, "progress"},
        {IndeterminateRole, "indeterminate"},
        {CanCancelRole, "canCancel"},
        {IssueCountRole, "issueCount"},
        {ErrorMessageRole, "errorMessage"},
        {ErrorCodeRole, "errorCode"},
        {ErrorTechnicalContextRole, "errorTechnicalContext"},
        {ErrorRemediationRole, "errorRemediation"},
        {ErrorRetryableRole, "errorRetryable"},
        {IssuesRole, "issues"},
    };
}

int JobListModel::activeCount() const noexcept
{
    return activeCount_;
}

bool JobListModel::hasActiveJobs() const noexcept
{
    return activeCount_ > 0;
}

QString JobListModel::activitySummary() const
{
    if (activeCount_ == 0)
    {
        return tr("No background tasks");
    }
    return tr("%n background task(s) active", nullptr, activeCount_);
}

QVariantMap JobListModel::detailsForJob(const QString& jobId) const
{
    const auto id = QUuid::fromString(jobId);
    const auto row = id.isNull() ? -1 : rowForId(id);
    if (row < 0)
    {
        return {};
    }

    const auto& job = jobs_.at(row);
    return {
        {QStringLiteral("jobId"), job.descriptor.id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("title"), job.descriptor.title},
        {QStringLiteral("state"), jobs::jobStateCode(job.state)},
        {QStringLiteral("failure"), serializeFailure(job.failure)},
        {QStringLiteral("issues"), serializeIssues(job.issues)},
    };
}

void JobListModel::addJob(const jobs::JobSnapshot& snapshot)
{
    if (rowForId(snapshot.descriptor.id) >= 0)
    {
        updateJob(snapshot);
        return;
    }

    const auto row = static_cast<int>(jobs_.size());
    beginInsertRows({}, row, row);
    jobs_.append(snapshot);
    endInsertRows();
    emit countChanged();
    recalculateActiveCount();
}

void JobListModel::updateJob(const jobs::JobSnapshot& snapshot)
{
    const auto row = rowForId(snapshot.descriptor.id);
    if (row < 0)
    {
        addJob(snapshot);
        return;
    }

    jobs_[row] = snapshot;
    const auto modelIndex = index(static_cast<int>(row), 0);
    emit dataChanged(modelIndex, modelIndex);
    recalculateActiveCount();
}

void JobListModel::removeJob(const QUuid& jobId)
{
    const auto row = rowForId(jobId);
    if (row < 0)
    {
        return;
    }

    beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
    jobs_.removeAt(row);
    endRemoveRows();
    emit countChanged();
    recalculateActiveCount();
}

void JobListModel::recalculateActiveCount()
{
    const auto count = static_cast<int>(
        std::ranges::count_if(jobs_, [](const auto& job) { return !jobs::isTerminal(job.state); }));
    if (count == activeCount_)
    {
        return;
    }
    activeCount_ = count;
    emit activeCountChanged();
}

qsizetype JobListModel::rowForId(const QUuid& jobId) const
{
    for (qsizetype row = 0; row < jobs_.size(); ++row)
    {
        if (jobs_.at(row).descriptor.id == jobId)
        {
            return row;
        }
    }
    return -1;
}

QString JobListModel::localizedState(const jobs::JobState state) const
{
    switch (state)
    {
    case jobs::JobState::Queued:
        return tr("Queued");
    case jobs::JobState::Running:
        return tr("Running");
    case jobs::JobState::Succeeded:
        return tr("Completed");
    case jobs::JobState::SucceededWithIssues:
        return tr("Completed with issues");
    case jobs::JobState::Failed:
        return tr("Failed");
    case jobs::JobState::CancelRequested:
        return tr("Canceling");
    case jobs::JobState::Canceled:
        return tr("Canceled");
    }
    return tr("Unknown");
}

} // namespace corax::presentation
