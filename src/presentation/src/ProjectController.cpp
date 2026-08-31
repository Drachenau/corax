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
    const auto path = localPathFromUrl(projectDirectory);
    if (path.isEmpty() || !beginOperation(tr("Creating project")))
    {
        return;
    }

    clearError();
    auto result = std::make_shared<ProjectOperationResult>();
    auto* service = &projectService_;
    auto job = std::make_unique<jobs::FunctionalJob>(
        projectJobDescriptor(QStringLiteral("project.create"), tr("Create project")),
        [service, result, path, displayName](jobs::JobContext&)
        {
            auto serviceResult = service->createProject(path, displayName);
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
                                      [self, result](const jobs::JobSnapshot&)
                                      {
                                          if (!self)
                                          {
                                              return;
                                          }
                                          self->endOperation();
                                          if (result->project)
                                          {
                                              self->applyProject(*result->project);
                                              emit self->projectOpened();
                                          }
                                          else if (result->error)
                                          {
                                              self->applyError(*result->error);
                                          }
                                      });
    if (id.isNull())
    {
        endOperation();
        applySubmissionError();
    }
}

void ProjectController::openProject(const QUrl& projectDirectory)
{
    const auto path = localPathFromUrl(projectDirectory);
    if (path.isEmpty() || !beginOperation(tr("Opening project")))
    {
        return;
    }

    clearError();
    auto result = std::make_shared<ProjectOperationResult>();
    auto* service = &projectService_;
    auto job = std::make_unique<jobs::FunctionalJob>(
        projectJobDescriptor(QStringLiteral("project.open"), tr("Open project")),
        [service, result, path](jobs::JobContext&)
        {
            auto serviceResult = service->openProject(path);
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
                                      [self, result](const jobs::JobSnapshot&)
                                      {
                                          if (!self)
                                          {
                                              return;
                                          }
                                          self->endOperation();
                                          if (result->project)
                                          {
                                              self->applyProject(*result->project);
                                              emit self->projectOpened();
                                          }
                                          else if (result->error)
                                          {
                                              self->applyError(*result->error);
                                          }
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
    auto* service = &projectService_;
    auto job = std::make_unique<jobs::FunctionalJob>(
        projectJobDescriptor(QStringLiteral("project.close"), tr("Close project")),
        [service, result](jobs::JobContext&)
        {
            auto serviceResult = service->closeProject();
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
                                      [self, result](const jobs::JobSnapshot&)
                                      {
                                          if (!self)
                                          {
                                              return;
                                          }
                                          self->endOperation();
                                          if (result->closed)
                                          {
                                              self->clearProject();
                                              emit self->projectClosed();
                                          }
                                          else if (result->error)
                                          {
                                              self->applyError(*result->error);
                                          }
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
