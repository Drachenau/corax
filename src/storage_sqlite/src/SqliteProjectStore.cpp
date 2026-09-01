// SPDX-License-Identifier: Apache-2.0

#include "corax/storage_sqlite/SqliteProjectStore.h"

#include <corax/build/BuildConfiguration.h>

#include "corax/domain/AppError.h"
#include "corax/storage_sqlite/ProjectWriterLock.h"
#include "corax/storage_sqlite/SqliteProjectDatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <utility>

namespace corax::storage_sqlite
{
namespace
{

domain::AppError projectError(const domain::ErrorCode code,
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

QString normalizedAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

constexpr std::array kRequiredDirectories{
    "managed/originals",
    "annotations/masks",
    "reports/imports",
    "reports/exports",
    "backups",
    "cache/thumbnails",
    "cache/previews",
    "cache/analysis",
    "tmp",
};

void removeOwnedProjectArtifacts(const QString& rootPath)
{
    QDir root(rootPath);
    static_cast<void>(
        QFile::remove(root.filePath(QString::fromLatin1(kDatabaseFileName) + "-shm")));
    static_cast<void>(
        QFile::remove(root.filePath(QString::fromLatin1(kDatabaseFileName) + "-wal")));
    static_cast<void>(QFile::remove(root.filePath(QString::fromLatin1(kDatabaseFileName))));
    static_cast<void>(QFile::remove(root.filePath(QString::fromLatin1(kManifestFileName))));

    for (auto it = kRequiredDirectories.rbegin(); it != kRequiredDirectories.rend(); ++it)
    {
        QString path = QString::fromLatin1(*it);
        while (!path.isEmpty())
        {
            static_cast<void>(root.rmdir(path));
            const qsizetype slash = path.lastIndexOf('/');
            path = slash >= 0 ? path.left(slash) : QString{};
        }
    }
}

domain::AppError lockFailureAfterProjectFailure(domain::AppError operationError,
                                                domain::AppError lockError)
{
    lockError.technicalContext =
        QStringLiteral("%1 Writer-lock handling failed after project failure %2: %3")
            .arg(lockError.technicalContext,
                 operationError.stableCode(),
                 operationError.technicalContext);
    return lockError;
}

domain::Result<domain::ProjectInfo> projectFailureAfterLockRelease(ProjectWriterLock& writerLock,
                                                                   domain::AppError operationError)
{
    auto released = writerLock.release();
    if (!released)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            lockFailureAfterProjectFailure(std::move(operationError), std::move(released).error()));
    }
    return domain::Result<domain::ProjectInfo>::failure(std::move(operationError));
}

domain::Result<domain::ProjectInfo> projectFailureAfterLockVerifiedCleanupAndRelease(
    ProjectWriterLock& writerLock,
    const QString& rootPath,
    domain::AppError operationError,
    const SqliteProjectStore::FailedCreateCleanupCheckpoint& cleanupCheckpoint)
{
    auto verified = writerLock.verifyOwnership();
    if (!verified)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            lockFailureAfterProjectFailure(std::move(operationError), std::move(verified).error()));
    }
    if (cleanupCheckpoint)
    {
        cleanupCheckpoint();
    }
    removeOwnedProjectArtifacts(rootPath);

    auto released = writerLock.release();
    if (!released)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            lockFailureAfterProjectFailure(std::move(operationError), std::move(released).error()));
    }
    return domain::Result<domain::ProjectInfo>::failure(std::move(operationError));
}

} // namespace

class SqliteProjectStore::Impl final
{
public:
    Impl(std::shared_ptr<IAtomicFileWriter> writer,
         InitialPathCheckpoint pathCheckpoint,
         FailedCreateCleanupCheckpoint cleanupCheckpoint)
        : manifests(std::move(writer)), initialPathCheckpoint(std::move(pathCheckpoint)),
          failedCreateCleanupCheckpoint(std::move(cleanupCheckpoint))
    {
    }

    ManifestStore manifests;
    ProjectWriterLock writerLock;
    std::optional<domain::ProjectInfo> current;
    InitialPathCheckpoint initialPathCheckpoint;
    FailedCreateCleanupCheckpoint failedCreateCleanupCheckpoint;
};

SqliteProjectStore::SqliteProjectStore(std::shared_ptr<IAtomicFileWriter> manifestWriter)
    : SqliteProjectStore(std::move(manifestWriter), {}, {})
{
}

SqliteProjectStore::SqliteProjectStore(std::shared_ptr<IAtomicFileWriter> manifestWriter,
                                       InitialPathCheckpoint initialPathCheckpoint)
    : SqliteProjectStore(std::move(manifestWriter), std::move(initialPathCheckpoint), {})
{
}

SqliteProjectStore::SqliteProjectStore(std::shared_ptr<IAtomicFileWriter> manifestWriter,
                                       InitialPathCheckpoint initialPathCheckpoint,
                                       FailedCreateCleanupCheckpoint failedCreateCleanupCheckpoint)
    : impl_(std::make_unique<Impl>(std::move(manifestWriter),
                                   std::move(initialPathCheckpoint),
                                   std::move(failedCreateCleanupCheckpoint)))
{
}

SqliteProjectStore::~SqliteProjectStore()
{
    const auto closed = closeProject();
    if (!closed)
    {
        impl_->writerLock.abandon();
    }
}

domain::Result<domain::ProjectInfo>
SqliteProjectStore::createProject(const application::NewProject& project)
{
    if (const auto recoveryError = impl_->writerLock.recoveryError())
    {
        return domain::Result<domain::ProjectInfo>::failure(*recoveryError);
    }
    if (impl_->current.has_value())
    {
        return domain::Result<domain::ProjectInfo>::failure(
            projectError(domain::ErrorCode::ProjectAlreadyOpen,
                         QStringLiteral("Close the current project before creating another one."),
                         QStringLiteral("SqliteProjectStore already has an open project."),
                         impl_->current->projectPath,
                         QStringLiteral("Close the current project and try again.")));
    }
    if (impl_->writerLock.ownsLock())
    {
        return domain::Result<domain::ProjectInfo>::failure(projectError(
            domain::ErrorCode::ProjectLockFailed,
            QStringLiteral("Corax must finish releasing its previous writer lock."),
            QStringLiteral("A previous failed project operation still owns a writer lock."),
            impl_->writerLock.lockPath(),
            QStringLiteral("Retry closing the project or restart Corax before continuing."),
            true));
    }
    if (project.projectId.isNull() || project.displayName.trimmed().isEmpty() ||
        !project.createdAtUtc.isValid())
    {
        return domain::Result<domain::ProjectInfo>::failure(projectError(
            domain::ErrorCode::ProjectCreateFailed,
            QStringLiteral("Corax cannot create a project with invalid metadata."),
            QStringLiteral("NewProject failed ID, display-name, or timestamp validation."),
            project.projectPath,
            QStringLiteral("Choose a valid directory and project name.")));
    }

    const QString rootPath = normalizedAbsolutePath(project.projectPath);
    QFileInfo rootInfo(rootPath);
    const bool rootInitiallyExisted = rootInfo.exists();
    if (rootInitiallyExisted && (!rootInfo.isDir() || rootInfo.isSymLink()))
    {
        return domain::Result<domain::ProjectInfo>::failure(
            projectError(domain::ErrorCode::ProjectPathInvalid,
                         QStringLiteral("The project path is not an ordinary directory."),
                         QStringLiteral("The selected path is a file or symbolic link."),
                         rootPath,
                         QStringLiteral("Choose a new or empty ordinary directory.")));
    }

    if (rootInitiallyExisted)
    {
        const QDir existing(rootPath);
        const auto entries = existing.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                                    QDir::NoDotAndDotDot);
        if (!entries.isEmpty())
        {
            return domain::Result<domain::ProjectInfo>::failure(projectError(
                domain::ErrorCode::ProjectAlreadyExists,
                QStringLiteral("The selected project directory is not empty."),
                QStringLiteral("Project creation never overwrites existing directory contents."),
                rootPath,
                QStringLiteral("Choose a new or empty directory.")));
        }
    }

    if (impl_->initialPathCheckpoint)
    {
        impl_->initialPathCheckpoint();
    }

    if (!rootInitiallyExisted)
    {
        QDir parent = rootInfo.dir();
        if ((!parent.exists() && !QDir().mkpath(parent.absolutePath())) ||
            !parent.mkdir(rootInfo.fileName()))
        {
            rootInfo.refresh();
            if (rootInfo.exists() && rootInfo.isDir() && !rootInfo.isSymLink())
            {
                return domain::Result<domain::ProjectInfo>::failure(projectError(
                    domain::ErrorCode::ProjectAlreadyExists,
                    QStringLiteral("Another operation claimed the project directory."),
                    QStringLiteral("The final project directory appeared during creation."),
                    rootPath,
                    QStringLiteral("Choose a new or empty directory and try again."),
                    true));
            }
            return domain::Result<domain::ProjectInfo>::failure(projectError(
                domain::ErrorCode::ProjectCreateFailed,
                QStringLiteral("Corax could not create the project directory."),
                QStringLiteral("The parent path or final directory could not be created."),
                rootPath,
                QStringLiteral("Check the parent directory permissions and try again."),
                true));
        }
    }

    auto lock = impl_->writerLock.acquire(rootPath, project.projectId);
    if (!lock)
    {
        return domain::Result<domain::ProjectInfo>::failure(std::move(lock).error());
    }

    const auto entriesAfterLock = QDir(rootPath).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                                               QDir::System | QDir::NoDotAndDotDot);
    const bool hasUnexpectedEntry =
        std::any_of(entriesAfterLock.cbegin(),
                    entriesAfterLock.cend(),
                    [](const QFileInfo& entry)
                    { return entry.fileName() != QString::fromLatin1(kWriterLockFileName); });
    if (hasUnexpectedEntry)
    {
        return projectFailureAfterLockRelease(
            impl_->writerLock,
            projectError(
                domain::ErrorCode::ProjectAlreadyExists,
                QStringLiteral("The selected project directory changed during creation."),
                QStringLiteral(
                    "An unexpected directory entry appeared before Corax initialized the project."),
                rootPath,
                QStringLiteral("Choose a new or empty directory and try again."),
                true));
    }

    QDir root(rootPath);
    for (const char* relativePath : kRequiredDirectories)
    {
        if (!root.mkpath(QString::fromLatin1(relativePath)))
        {
            return projectFailureAfterLockVerifiedCleanupAndRelease(
                impl_->writerLock,
                rootPath,
                projectError(
                    domain::ErrorCode::ProjectCreateFailed,
                    QStringLiteral("Corax could not create the project structure."),
                    QStringLiteral("Failed to create required directory '%1'.")
                        .arg(QString::fromLatin1(relativePath)),
                    rootPath,
                    QStringLiteral("Check directory permissions and available disk space."),
                    true),
                impl_->failedCreateCleanupCheckpoint);
        }
    }

    const ProjectManifest manifest{
        .projectId = project.projectId,
        .displayName = project.displayName.trimmed(),
        .databaseFile = QString::fromLatin1(kDatabaseFileName),
        .createdAtUtc = project.createdAtUtc.toUTC(),
        .minimumCoraxVersion = QString::fromLatin1(build::kApplicationVersion),
        .preservedFields = {},
    };
    const QString manifestPath = root.filePath(QString::fromLatin1(kManifestFileName));
    auto manifestWritten = impl_->manifests.write(manifestPath, manifest);
    if (!manifestWritten)
    {
        auto operationError = std::move(manifestWritten).error();
        return projectFailureAfterLockVerifiedCleanupAndRelease(
            impl_->writerLock,
            rootPath,
            std::move(operationError),
            impl_->failedCreateCleanupCheckpoint);
    }

    const domain::ProjectInfo initialInfo{
        .projectId = project.projectId,
        .displayName = project.displayName.trimmed(),
        .projectPath = rootPath,
        .createdAtUtc = project.createdAtUtc.toUTC(),
        .revision = 0,
    };
    const QString databasePath = root.filePath(QString::fromLatin1(kDatabaseFileName));
    auto initialized = SqliteProjectDatabase::initializeNew(databasePath, initialInfo);
    if (!initialized)
    {
        auto operationError = std::move(initialized).error();
        return projectFailureAfterLockVerifiedCleanupAndRelease(
            impl_->writerLock,
            rootPath,
            std::move(operationError),
            impl_->failedCreateCleanupCheckpoint);
    }

    auto database = std::move(initialized).value();
    auto storedInfo = database->projectInfo();
    if (!storedInfo)
    {
        auto operationError = std::move(storedInfo).error();
        database.reset();
        return projectFailureAfterLockVerifiedCleanupAndRelease(
            impl_->writerLock,
            rootPath,
            std::move(operationError),
            impl_->failedCreateCleanupCheckpoint);
    }
    database.reset();

    impl_->current = storedInfo.value();
    return domain::Result<domain::ProjectInfo>::success(storedInfo.value());
}

domain::Result<domain::ProjectInfo> SqliteProjectStore::openProject(const QString& projectDirectory)
{
    if (const auto recoveryError = impl_->writerLock.recoveryError())
    {
        return domain::Result<domain::ProjectInfo>::failure(*recoveryError);
    }
    if (impl_->current.has_value())
    {
        return domain::Result<domain::ProjectInfo>::failure(
            projectError(domain::ErrorCode::ProjectAlreadyOpen,
                         QStringLiteral("Close the current project before opening another one."),
                         QStringLiteral("SqliteProjectStore already has an open project."),
                         impl_->current->projectPath,
                         QStringLiteral("Close the current project and try again.")));
    }
    if (impl_->writerLock.ownsLock())
    {
        return domain::Result<domain::ProjectInfo>::failure(projectError(
            domain::ErrorCode::ProjectLockFailed,
            QStringLiteral("Corax must finish releasing its previous writer lock."),
            QStringLiteral("A previous failed project operation still owns a writer lock."),
            impl_->writerLock.lockPath(),
            QStringLiteral("Retry closing the project or restart Corax before continuing."),
            true));
    }

    const QString rootPath = normalizedAbsolutePath(projectDirectory);
    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink())
    {
        return domain::Result<domain::ProjectInfo>::failure(projectError(
            domain::ErrorCode::ProjectPathInvalid,
            QStringLiteral("The selected project directory is not available."),
            QStringLiteral("The path is missing, is not a directory, or is a symbolic link."),
            rootPath,
            QStringLiteral("Choose an existing ordinary Corax project directory.")));
    }

    QDir root(rootPath);
    auto manifest = impl_->manifests.read(root.filePath(QString::fromLatin1(kManifestFileName)));
    if (!manifest)
    {
        return domain::Result<domain::ProjectInfo>::failure(std::move(manifest).error());
    }

    auto lock = impl_->writerLock.acquire(rootPath, manifest.value().projectId);
    if (!lock)
    {
        return domain::Result<domain::ProjectInfo>::failure(std::move(lock).error());
    }

    auto opened = SqliteProjectDatabase::openExisting(
        root.filePath(manifest.value().databaseFile), rootPath, manifest.value().projectId);
    if (!opened)
    {
        return projectFailureAfterLockRelease(impl_->writerLock, std::move(opened).error());
    }

    auto database = std::move(opened).value();
    auto storedInfo = database->projectInfo();
    if (!storedInfo)
    {
        auto operationError = std::move(storedInfo).error();
        database.reset();
        return projectFailureAfterLockRelease(impl_->writerLock, std::move(operationError));
    }
    database.reset();

    impl_->current = storedInfo.value();
    return domain::Result<domain::ProjectInfo>::success(storedInfo.value());
}

domain::Result<void> SqliteProjectStore::closeProject()
{
    auto released = impl_->writerLock.release();
    if (!released)
    {
        return released;
    }
    impl_->current.reset();
    return domain::Result<void>::success();
}

std::optional<domain::ProjectInfo> SqliteProjectStore::currentProject() const
{
    return impl_->current;
}

bool SqliteProjectStore::recoveryRequired() const noexcept
{
    return impl_->writerLock.recoveryRequired();
}

} // namespace corax::storage_sqlite
