// SPDX-License-Identifier: Apache-2.0

#include "corax/application/ProjectService.h"

#include "corax/domain/AppError.h"

#include <exception>
#include <utility>

namespace corax::application
{
namespace
{

domain::AppError invalidArgument(QString userMessage, QString technicalContext)
{
    return {
        .code = domain::ErrorCode::InvalidArgument,
        .userMessage = std::move(userMessage),
        .technicalContext = std::move(technicalContext),
        .remediation = QStringLiteral("Choose a project directory and enter a project name."),
        .affectedPath = {},
        .retryable = false,
    };
}

domain::AppError dependencyFailure(const QString& detail)
{
    return {
        .code = domain::ErrorCode::InternalError,
        .userMessage = QStringLiteral("Corax could not prepare the project operation."),
        .technicalContext = detail,
        .remediation = QStringLiteral("Restart Corax and try the operation again."),
        .affectedPath = {},
        .retryable = false,
    };
}

} // namespace

ProjectService::ProjectService(IProjectStore& store)
    : ProjectService(
          store, [] { return QUuid::createUuid(); }, [] { return QDateTime::currentDateTimeUtc(); })
{
}

ProjectService::ProjectService(IProjectStore& store, IdGenerator idGenerator, Clock clock)
    : store_(store), idGenerator_(std::move(idGenerator)), clock_(std::move(clock))
{
}

domain::Result<domain::ProjectInfo> ProjectService::createProject(const QString& projectDirectory,
                                                                  const QString& displayName)
{
    if (projectDirectory.trimmed().isEmpty())
    {
        return domain::Result<domain::ProjectInfo>::failure(
            invalidArgument(QStringLiteral("Choose where to create the project."),
                            QStringLiteral("The project directory is empty.")));
    }

    const QString normalizedName = displayName.trimmed();
    if (normalizedName.isEmpty())
    {
        return domain::Result<domain::ProjectInfo>::failure(invalidArgument(
            QStringLiteral("Enter a project name."),
            QStringLiteral("The project display name is empty or contains only whitespace.")));
    }
    if (normalizedName.size() > 256)
    {
        return domain::Result<domain::ProjectInfo>::failure(invalidArgument(
            QStringLiteral("The project name is too long."),
            QStringLiteral("The project display name exceeds 256 Unicode code units.")));
    }

    try
    {
        const QUuid projectId = idGenerator_();
        QDateTime createdAt = clock_();
        if (projectId.isNull())
        {
            return domain::Result<domain::ProjectInfo>::failure(dependencyFailure(
                QStringLiteral("The configured project ID generator returned a null UUID.")));
        }
        if (!createdAt.isValid())
        {
            return domain::Result<domain::ProjectInfo>::failure(dependencyFailure(
                QStringLiteral("The configured clock returned an invalid timestamp.")));
        }

        createdAt = createdAt.toUTC();
        return store_.createProject({
            .projectId = projectId,
            .displayName = normalizedName,
            .projectPath = projectDirectory,
            .createdAtUtc = createdAt,
        });
    }
    catch (const std::exception& exception)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            dependencyFailure(QString::fromUtf8(exception.what())));
    }
    catch (...)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            dependencyFailure(QStringLiteral("An unknown dependency exception occurred.")));
    }
}

domain::Result<domain::ProjectInfo> ProjectService::openProject(const QString& projectDirectory)
{
    if (projectDirectory.trimmed().isEmpty())
    {
        return domain::Result<domain::ProjectInfo>::failure(
            invalidArgument(QStringLiteral("Choose a project to open."),
                            QStringLiteral("The project directory is empty.")));
    }

    try
    {
        return store_.openProject(projectDirectory);
    }
    catch (const std::exception& exception)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            dependencyFailure(QString::fromUtf8(exception.what())));
    }
    catch (...)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            dependencyFailure(QStringLiteral("An unknown dependency exception occurred.")));
    }
}

domain::Result<void> ProjectService::closeProject()
{
    try
    {
        return store_.closeProject();
    }
    catch (const std::exception& exception)
    {
        return domain::Result<void>::failure(
            dependencyFailure(QString::fromUtf8(exception.what())));
    }
    catch (...)
    {
        return domain::Result<void>::failure(
            dependencyFailure(QStringLiteral("An unknown dependency exception occurred.")));
    }
}

std::optional<domain::ProjectInfo> ProjectService::currentProject() const
{
    return store_.currentProject();
}

bool ProjectService::recoveryRequired() const noexcept
{
    return store_.recoveryRequired();
}

} // namespace corax::application
