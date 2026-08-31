// SPDX-License-Identifier: Apache-2.0

#include "corax/storage_sqlite/ProjectWriterLock.h"

#include "corax/domain/AppError.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include <utility>

namespace corax::storage_sqlite
{
namespace
{

domain::AppError lockError(const domain::ErrorCode code,
                           QString userMessage,
                           QString technicalContext,
                           const QString& path,
                           QString remediation,
                           const bool retryable = false)
{
    return {
        .code = code,
        .userMessage = std::move(userMessage),
        .technicalContext = std::move(technicalContext),
        .remediation = std::move(remediation),
        .affectedPath = path,
        .retryable = retryable,
    };
}

QString describeExistingLock(const QString& path)
{
    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly))
    {
        return QStringLiteral("The writer lock exists but cannot be read: %1")
            .arg(existing.errorString());
    }
    if (existing.size() > 64 * 1024)
    {
        return QStringLiteral("The writer lock exists but exceeds the 64 KiB safety limit.");
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(existing.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return QStringLiteral("The writer lock exists but is malformed: %1")
            .arg(parseError.errorString());
    }

    const QJsonObject object = document.object();
    return QStringLiteral(
               "Writer lock owner: host=%1, processId=%2, startedAt=%3, applicationVersion=%4.")
        .arg(object.value(QStringLiteral("host")).toString(QStringLiteral("unknown")))
        .arg(object.value(QStringLiteral("processId")).toVariant().toLongLong())
        .arg(object.value(QStringLiteral("startedAt")).toString(QStringLiteral("unknown")))
        .arg(
            object.value(QStringLiteral("applicationVersion")).toString(QStringLiteral("unknown")));
}

} // namespace

ProjectWriterLock::~ProjectWriterLock()
{
    if (ownsLock())
    {
        const auto released = release();
        if (!released)
        {
            abandon();
        }
    }
}

domain::Result<void> ProjectWriterLock::acquire(const QString& projectDirectory,
                                                const QUuid& projectId)
{
    if (recoveryRequired())
    {
        return domain::Result<void>::failure(*recoveryError_);
    }
    if (ownsLock())
    {
        return domain::Result<void>::failure(
            lockError(domain::ErrorCode::ProjectLockFailed,
                      QStringLiteral("Corax already owns a project writer lock."),
                      QStringLiteral("The lock object cannot acquire a second project."),
                      lockPath_,
                      QStringLiteral("Close the current project before opening another one.")));
    }
    if (projectId.isNull())
    {
        return domain::Result<void>::failure(
            lockError(domain::ErrorCode::ProjectLockFailed,
                      QStringLiteral("Corax cannot lock a project without a valid ID."),
                      QStringLiteral("A null project UUID was supplied to the lock."),
                      projectDirectory,
                      QStringLiteral("Repair or recreate the project manifest.")));
    }

    const QString path = QDir(projectDirectory).filePath(QString::fromLatin1(kWriterLockFileName));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        if (QFile::exists(path))
        {
            return domain::Result<void>::failure(
                lockError(domain::ErrorCode::ProjectLocked,
                          QStringLiteral("This project is already open for writing."),
                          describeExistingLock(path),
                          path,
                          QStringLiteral("Close the other Corax instance. If it crashed, verify "
                                         "that no writer is active before removing the lock file."),
                          true));
        }
        return domain::Result<void>::failure(
            lockError(domain::ErrorCode::ProjectLockFailed,
                      QStringLiteral("Corax could not create the project writer lock."),
                      file.errorString(),
                      path,
                      QStringLiteral("Check project directory permissions and try again."),
                      true));
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString version = QCoreApplication::applicationVersion();
    if (version.isEmpty())
    {
        version = QStringLiteral("0.1.0");
    }

    const QJsonObject object{
        {QStringLiteral("format"), QStringLiteral("org.corax.writer-lock")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("projectId"), projectId.toString(QUuid::WithoutBraces)},
        {QStringLiteral("processId"), QCoreApplication::applicationPid()},
        {QStringLiteral("host"), QSysInfo::machineHostName()},
        {QStringLiteral("applicationVersion"), version},
        {QStringLiteral("startedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("ownershipToken"), token},
    };
    const QByteArray contents = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(contents) != contents.size() || !file.flush())
    {
        const QString detail = file.errorString();
        file.close();
        static_cast<void>(QFile::remove(path));
        return domain::Result<void>::failure(lockError(
            domain::ErrorCode::ProjectLockFailed,
            QStringLiteral("Corax could not finish the project writer lock."),
            detail,
            path,
            QStringLiteral("Check available disk space and project directory permissions."),
            true));
    }
    file.close();

    lockPath_ = path;
    ownershipToken_ = token;
    projectId_ = projectId;
    state_ = State::Owned;
    return domain::Result<void>::success();
}

domain::Result<void> ProjectWriterLock::release()
{
    if (state_ == State::Unowned)
    {
        return domain::Result<void>::success();
    }
    if (recoveryRequired())
    {
        return domain::Result<void>::failure(*recoveryError_);
    }

    QFile file(lockPath_);
    if (!file.open(QIODevice::ReadOnly))
    {
        auto error = lockError(
            domain::ErrorCode::ProjectLockOwnershipLost,
            QStringLiteral("Corax could not verify its project writer lock."),
            QStringLiteral("The writer lock for project %1 could not be read: %2")
                .arg(projectId_.toString(QUuid::WithoutBraces), file.errorString()),
            lockPath_,
            QStringLiteral(
                "Verify that no other writer owns this project before changing the lock file."));
        markOwnershipLost(error);
        return domain::Result<void>::failure(error);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject{};
    const bool matches =
        parseError.error == QJsonParseError::NoError &&
        object.value(QStringLiteral("projectId")).toString() ==
            projectId_.toString(QUuid::WithoutBraces) &&
        object.value(QStringLiteral("ownershipToken")).toString() == ownershipToken_;
    if (!matches)
    {
        const QString observedProjectId =
            object.value(QStringLiteral("projectId")).toString(QStringLiteral("unavailable"));
        auto error = lockError(
            domain::ErrorCode::ProjectLockOwnershipLost,
            QStringLiteral("Corax no longer owns the project writer lock."),
            QStringLiteral("The on-disk lock token or project ID changed after acquisition. "
                           "Expected project ID %1; observed project ID %2; parse status: %3.")
                .arg(projectId_.toString(QUuid::WithoutBraces),
                     observedProjectId,
                     parseError.errorString()),
            lockPath_,
            QStringLiteral("Verify the active writer before changing the lock file."));
        markOwnershipLost(error);
        return domain::Result<void>::failure(error);
    }

    if (!QFile::remove(lockPath_))
    {
        return domain::Result<void>::failure(lockError(
            domain::ErrorCode::ProjectLockFailed,
            QStringLiteral("Corax could not release the project writer lock."),
            QStringLiteral("QFile::remove returned false for the owned lock file."),
            lockPath_,
            QStringLiteral("Check project directory permissions before opening the project again."),
            true));
    }

    clearLocalState();
    return domain::Result<void>::success();
}

void ProjectWriterLock::abandon() noexcept
{
    clearLocalState();
}

std::optional<domain::AppError> ProjectWriterLock::recoveryError() const
{
    return recoveryError_;
}

void ProjectWriterLock::markOwnershipLost(domain::AppError error)
{
    state_ = State::OwnershipLost;
    ownershipToken_.clear();
    recoveryError_ = std::move(error);
}

void ProjectWriterLock::clearLocalState() noexcept
{
    state_ = State::Unowned;
    lockPath_.clear();
    ownershipToken_.clear();
    projectId_ = {};
    recoveryError_.reset();
}

} // namespace corax::storage_sqlite
