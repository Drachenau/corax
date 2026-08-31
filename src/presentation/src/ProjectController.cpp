#include <corax/presentation/ProjectController.h>

#include <corax/jobs/Job.h>

#include <QPointer>

#include <memory>
#include <optional>
#include <utility>

namespace corax::presentation
{
namespace
{

struct ProjectOperationResult
{
    std::optional<domain::ProjectInfo> project;
    std::optional<domain::AppError> error;
    std::optional<bool> recoveryRequired;
    QString affectedPath;
    bool closed{false};
};

jobs::JobFailure toJobFailure(const domain::AppError& error)
{
    return {
        .code = error.stableCode(),
        .userMessage = error.userMessage,
        .technicalContext = error.technicalContext,
        .remediation = error.remediation,
        .retryable = error.retryable,
    };
}

jobs::JobDescriptor projectJobDescriptor(QString type, QString title)
{
    return {
        .id = QUuid::createUuid(),
        .type = std::move(type),
        .owner = QStringLiteral("corax_application"),
        .payloadVersion = 1,
        .title = std::move(title),
        .projectId = {},
        .priority = jobs::JobPriority::UserBlocking,
        .requiredLanes = {jobs::ResourceLane::Io, jobs::ResourceLane::DatabaseWrite},
        .cancellable = false,
    };
}

bool isSuccessfulTerminalState(const jobs::JobState state) noexcept
{
    return state == jobs::JobState::Succeeded || state == jobs::JobState::SucceededWithIssues;
}

} // namespace

ProjectController::ProjectController(application::ProjectService& projectService,
                                     jobs::JobScheduler& scheduler,
                                     QObject* parent)
    : QObject(parent), projectService_(projectService), scheduler_(scheduler)
{
}

bool ProjectController::hasProject() const noexcept
{
    return project_.has_value();
}

QString ProjectController::projectName() const
{
    return project_ ? project_->displayName : QString{};
}

QString ProjectController::projectId() const
{
    return project_ ? project_->projectId.toString(QUuid::WithoutBraces) : QString{};
}

QUrl ProjectController::projectLocation() const
{
    return project_ ? QUrl::fromLocalFile(project_->projectPath) : QUrl{};
}

qint64 ProjectController::projectRevision() const noexcept
{
    return project_ ? project_->revision : 0;
}

bool ProjectController::busy() const noexcept
{
    return busy_;
}

QString ProjectController::currentOperation() const
{
    return currentOperation_;
}

bool ProjectController::recoveryRequired() const noexcept
{
    return recoveryRequired_;
}

bool ProjectController::hasError() const noexcept
{
    return hasError_;
}

QString ProjectController::errorCode() const
{
    return hasError_ ? error_.stableCode() : QString{};
}

QString ProjectController::errorMessage() const
{
    return hasError_ ? error_.userMessage : QString{};
}

QString ProjectController::errorTechnicalContext() const
{
    return hasError_ ? error_.technicalContext : QString{};
}

QString ProjectController::errorRemediation() const
{
    return hasError_ ? error_.remediation : QString{};
}

QString ProjectController::errorAffectedPath() const
{
    return hasError_ ? error_.affectedPath : QString{};
}

bool ProjectController::errorRetryable() const noexcept
{
    return hasError_ && error_.retryable;
}

void ProjectController::createProject(const QUrl& projectDirectory, const QString& displayName)
{
    if (rejectIfRecoveryRequired())
    {
        return;
    }
    const auto path = localPathFromUrl(projectDirectory);
    if (path.isEmpty() || !beginOperation(tr("Creating project")))
    {
        return;
    }

    clearError();
    auto result = std::make_shared<ProjectOperationResult>();
    result->affectedPath = path;
    auto* service = &projectService_;
    const auto workerEntryHook = workerEntryHook_;
    auto job = std::make_unique<jobs::FunctionalJob>(
        projectJobDescriptor(QStringLiteral("project.create"), tr("Create project")),
        [service, result, path, displayName, workerEntryHook](jobs::JobContext&)
        {
            if (workerEntryHook)
            {
                workerEntryHook(u"project.create");
            }
            auto serviceResult = service->createProject(path, displayName);
            result->recoveryRequired = service->recoveryRequired();
            if (!serviceResult)
            {
                result->error.emplace(std::move(serviceResult).error());
                return jobs::JobOutcome::failed(toJobFailure(*result->error));
            }
            result->project.emplace(std::move(serviceResult).value());
            return jobs::JobOutcome::succeeded();
        });

    QPointer<ProjectController> self{this};
    const auto id = scheduler_.submit(std::move(job),
                                      [self, result](const jobs::JobSnapshot& snapshot)
                                      {
                                          if (!self)
                                          {
                                              return;
                                          }
                                          self->completeOperation(snapshot,
                                                                  result->project,
                                                                  result->error,
                                                                  result->closed,
                                                                  result->affectedPath,
                                                                  result->recoveryRequired,
                                                                  CompletionAction::ApplyProject);
                                      });
    if (id.isNull())
    {
        endOperation();
        applySubmissionError();
    }
}

void ProjectController::openProject(const QUrl& projectDirectory)
{
    if (rejectIfRecoveryRequired())
    {
        return;
    }
    const auto path = localPathFromUrl(projectDirectory);
    if (path.isEmpty() || !beginOperation(tr("Opening project")))
    {
        return;
    }

    clearError();
    auto result = std::make_shared<ProjectOperationResult>();
    result->affectedPath = path;
    auto* service = &projectService_;
    const auto workerEntryHook = workerEntryHook_;
    auto job = std::make_unique<jobs::FunctionalJob>(
        projectJobDescriptor(QStringLiteral("project.open"), tr("Open project")),
        [service, result, path, workerEntryHook](jobs::JobContext&)
        {
            if (workerEntryHook)
            {
                workerEntryHook(u"project.open");
            }
            auto serviceResult = service->openProject(path);
            result->recoveryRequired = service->recoveryRequired();
            if (!serviceResult)
            {
                result->error.emplace(std::move(serviceResult).error());
                return jobs::JobOutcome::failed(toJobFailure(*result->error));
            }
            result->project.emplace(std::move(serviceResult).value());
            return jobs::JobOutcome::succeeded();
        });

    QPointer<ProjectController> self{this};
    const auto id = scheduler_.submit(std::move(job),
                                      [self, result](const jobs::JobSnapshot& snapshot)
                                      {
                                          if (!self)
                                          {
                                              return;
                                          }
                                          self->completeOperation(snapshot,
                                                                  result->project,
                                                                  result->error,
                                                                  result->closed,
                                                                  result->affectedPath,
                                                                  result->recoveryRequired,
                                                                  CompletionAction::ApplyProject);
                                      });
    if (id.isNull())
    {
        endOperation();
        applySubmissionError();
    }
}

void ProjectController::closeProject()
{
    if (!beginOperation(tr("Closing project")))
    {
        return;
    }

    clearError();
    auto result = std::make_shared<ProjectOperationResult>();
    result->affectedPath = project_ ? project_->projectPath : QString{};
    auto* service = &projectService_;
    const auto workerEntryHook = workerEntryHook_;
    auto job = std::make_unique<jobs::FunctionalJob>(
        projectJobDescriptor(QStringLiteral("project.close"), tr("Close project")),
        [service, result, workerEntryHook](jobs::JobContext&)
        {
            if (workerEntryHook)
            {
                workerEntryHook(u"project.close");
            }
            auto serviceResult = service->closeProject();
            result->recoveryRequired = service->recoveryRequired();
            if (!serviceResult)
            {
                result->error.emplace(std::move(serviceResult).error());
                return jobs::JobOutcome::failed(toJobFailure(*result->error));
            }
            result->closed = true;
            return jobs::JobOutcome::succeeded();
        });

    QPointer<ProjectController> self{this};
    const auto id = scheduler_.submit(std::move(job),
                                      [self, result](const jobs::JobSnapshot& snapshot)
                                      {
                                          if (!self)
                                          {
                                              return;
                                          }
                                          self->completeOperation(snapshot,
                                                                  result->project,
                                                                  result->error,
                                                                  result->closed,
                                                                  result->affectedPath,
                                                                  result->recoveryRequired,
                                                                  CompletionAction::ClearProject);
                                      });
    if (id.isNull())
    {
        endOperation();
        applySubmissionError();
    }
}

void ProjectController::clearError()
{
    if (!hasError_)
    {
        return;
    }
    hasError_ = false;
    error_ = {};
    emit errorChanged();
}

QString ProjectController::localPathFromUrl(const QUrl& url)
{
    if (url.isValid() && url.isLocalFile() && !url.toLocalFile().trimmed().isEmpty())
    {
        return url.toLocalFile();
    }

    applyError({
        .code = domain::ErrorCode::ProjectPathInvalid,
        .userMessage = tr("Choose a local project folder."),
        .technicalContext = tr("The selected location is not a valid local file URL."),
        .remediation = tr("Choose a folder on this computer and try again."),
        .affectedPath = url.toDisplayString(),
        .retryable = true,
    });
    return {};
}

bool ProjectController::beginOperation(QString operation)
{
    if (busy_)
    {
        applyError({
            .code = domain::ErrorCode::InvalidArgument,
            .userMessage = tr("Another project operation is still running."),
            .technicalContext =
                QStringLiteral("ProjectController rejected overlapping operations."),
            .remediation = tr("Wait for the current operation to finish, then try again."),
            .affectedPath = {},
            .retryable = true,
        });
        return false;
    }

    busy_ = true;
    currentOperation_ = std::move(operation);
    emit busyChanged();
    return true;
}

bool ProjectController::rejectIfRecoveryRequired()
{
    if (!recoveryRequired_)
    {
        return false;
    }

    applyError({
        .code = domain::ErrorCode::ProjectLockOwnershipLost,
        .userMessage = tr("This project needs writer-lock recovery."),
        .technicalContext =
            QStringLiteral("ProjectController blocked a new project operation while writer-lock "
                           "recovery is required."),
        .remediation = tr("Restart Corax after you verify which process owns the project lock."),
        .affectedPath = project_ ? project_->projectPath : QString{},
        .retryable = false,
    });
    return true;
}

void ProjectController::endOperation()
{
    if (!busy_)
    {
        return;
    }
    busy_ = false;
    currentOperation_.clear();
    emit busyChanged();
}

void ProjectController::completeOperation(const jobs::JobSnapshot& snapshot,
                                          const std::optional<domain::ProjectInfo>& project,
                                          const std::optional<domain::AppError>& error,
                                          const bool closed,
                                          const QString& affectedPath,
                                          const std::optional<bool>& recoveryRequired,
                                          const CompletionAction action)
{
    endOperation();

    if (recoveryRequired)
    {
        setRecoveryRequired(*recoveryRequired);
    }
    if (error)
    {
        if (error->code == domain::ErrorCode::ProjectLockOwnershipLost)
        {
            setRecoveryRequired(true);
        }
        applyError(*error);
        return;
    }

    const bool hasSuccessfulResult =
        action == CompletionAction::ApplyProject ? project.has_value() : closed;
    if (hasSuccessfulResult)
    {
        if (!isSuccessfulTerminalState(snapshot.state))
        {
            applyError({
                .code = domain::ErrorCode::InternalError,
                .userMessage = tr("Corax could not confirm the project operation result."),
                .technicalContext =
                    QStringLiteral("Project operation %1 produced a domain success while the "
                                   "scheduler ended in state %2.")
                        .arg(snapshot.descriptor.type, jobs::jobStateCode(snapshot.state)),
                .remediation = tr("Restart Corax and verify the project before trying again."),
                .affectedPath = affectedPath,
                .retryable = false,
            });
            return;
        }

        if (action == CompletionAction::ApplyProject)
        {
            applyProject(*project);
            emit projectOpened();
        }
        else
        {
            clearProject();
            emit projectClosed();
        }
        return;
    }

    if (snapshot.state == jobs::JobState::Failed)
    {
        const QString schedulerCode = snapshot.failure.code.isEmpty()
                                          ? QStringLiteral("jobs.unspecified_failure")
                                          : snapshot.failure.code;
        const QString schedulerContext =
            snapshot.failure.technicalContext.isEmpty()
                ? QStringLiteral("The scheduler did not provide technical failure context.")
                : snapshot.failure.technicalContext;
        applyError({
            .code = domain::ErrorCode::ProjectOperationFailed,
            .userMessage = snapshot.failure.userMessage.isEmpty()
                               ? tr("The project operation failed.")
                               : snapshot.failure.userMessage,
            .technicalContext =
                QStringLiteral("Scheduler failure %1: %2").arg(schedulerCode, schedulerContext),
            .remediation = snapshot.failure.remediation.isEmpty()
                               ? tr("Try the project operation again.")
                               : snapshot.failure.remediation,
            .affectedPath = affectedPath,
            .retryable = snapshot.failure.code.isEmpty() || snapshot.failure.retryable,
        });
        return;
    }

    if (snapshot.state == jobs::JobState::Canceled)
    {
        applyError({
            .code = domain::ErrorCode::ProjectOperationCanceled,
            .userMessage = tr("The project operation was canceled."),
            .technicalContext =
                QStringLiteral("The scheduler canceled %1 before it produced a domain result.")
                    .arg(snapshot.descriptor.type),
            .remediation =
                tr("Try the project operation again after Corax finishes shutting down."),
            .affectedPath = affectedPath,
            .retryable = true,
        });
        return;
    }

    applyError({
        .code = domain::ErrorCode::InternalError,
        .userMessage = tr("Corax received an incomplete project operation result."),
        .technicalContext =
            QStringLiteral("Project operation %1 ended in state %2 without a domain result.")
                .arg(snapshot.descriptor.type, jobs::jobStateCode(snapshot.state)),
        .remediation = tr("Restart Corax and verify the project before trying again."),
        .affectedPath = affectedPath,
        .retryable = false,
    });
}

void ProjectController::applyProject(const domain::ProjectInfo& project)
{
    project_ = project;
    emit projectChanged();
}

void ProjectController::clearProject()
{
    if (!project_)
    {
        return;
    }
    project_.reset();
    emit projectChanged();
}

void ProjectController::setRecoveryRequired(const bool required)
{
    if (recoveryRequired_ == required)
    {
        return;
    }
    recoveryRequired_ = required;
    emit recoveryRequiredChanged();
}

void ProjectController::applyError(const domain::AppError& error)
{
    error_ = error;
    hasError_ = true;
    emit errorChanged();
}

void ProjectController::applySubmissionError()
{
    applyError({
        .code = domain::ErrorCode::InternalError,
        .userMessage = tr("Corax could not start the project operation."),
        .technicalContext = QStringLiteral("The job scheduler is not accepting work."),
        .remediation = tr("Restart Corax and try again."),
        .affectedPath = {},
        .retryable = true,
    });
}

} // namespace corax::presentation
