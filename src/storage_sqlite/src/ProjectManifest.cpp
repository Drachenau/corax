// SPDX-License-Identifier: Apache-2.0

#include "corax/storage_sqlite/ProjectManifest.h"

#include <corax/build/BuildConfiguration.h>

#include "corax/domain/AppError.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QVersionNumber>

#include <utility>

namespace corax::storage_sqlite
{
namespace
{

constexpr qint64 kMaximumManifestBytes = 1024 * 1024;

QVersionNumber currentCoraxVersion()
{
    return QVersionNumber::fromString(QString::fromLatin1(build::kApplicationVersion));
}

bool parseCanonicalVersion(const QString& text, QVersionNumber& version)
{
    qsizetype suffixIndex = 0;
    version = QVersionNumber::fromString(text, &suffixIndex);
    return suffixIndex == text.size() && version.segmentCount() == 3 && version.toString() == text;
}

domain::AppError
manifestError(const domain::ErrorCode code,
              QString userMessage,
              QString technicalContext,
              const QString& path,
              QString remediation = QStringLiteral("Choose a valid Corax project and try again."))
{
    return {
        .code = code,
        .userMessage = std::move(userMessage),
        .technicalContext = std::move(technicalContext),
        .remediation = std::move(remediation),
        .affectedPath = path,
        .retryable = false,
    };
}

domain::Result<void>
requireString(const QJsonObject& object, const QString& key, const QString& path)
{
    if (!object.contains(key) || !object.value(key).isString())
    {
        return domain::Result<void>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("The project manifest is not valid."),
            QStringLiteral("Required field '%1' is missing or is not a string.").arg(key),
            path));
    }
    return domain::Result<void>::success();
}

} // namespace

bool ProjectManifest::isValid() const
{
    QVersionNumber minimumVersion;
    const QJsonValue sourceRoots = preservedFields.value(QStringLiteral("sourceRoots"));
    const bool sourceRootsAreSupported = !preservedFields.contains(QStringLiteral("sourceRoots")) ||
                                         (sourceRoots.isArray() && sourceRoots.toArray().isEmpty());
    return !projectId.isNull() && !displayName.trimmed().isEmpty() && displayName.size() <= 256 &&
           databaseFile == QString::fromLatin1(kDatabaseFileName) && createdAtUtc.isValid() &&
           parseCanonicalVersion(minimumCoraxVersion, minimumVersion) &&
           minimumVersion <= currentCoraxVersion() && sourceRootsAreSupported;
}

domain::Result<void> QSaveFileWriter::writeAtomically(const QString& path,
                                                      const QByteArray& contents)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
    {
        return domain::Result<void>::failure(manifestError(
            domain::ErrorCode::ManifestWriteFailed,
            QStringLiteral("Corax could not write the project manifest."),
            file.errorString(),
            path,
            QStringLiteral(
                "Check directory permissions and available disk space, then try again.")));
    }

    if (file.write(contents) != contents.size())
    {
        file.cancelWriting();
        return domain::Result<void>::failure(
            manifestError(domain::ErrorCode::ManifestWriteFailed,
                          QStringLiteral("Corax could not finish writing the project manifest."),
                          file.errorString(),
                          path,
                          QStringLiteral("Check available disk space and try again.")));
    }

    if (!file.commit())
    {
        return domain::Result<void>::failure(manifestError(
            domain::ErrorCode::ManifestWriteFailed,
            QStringLiteral("Corax could not replace the project manifest atomically."),
            file.errorString(),
            path,
            QStringLiteral("Check that the project directory supports atomic file replacement.")));
    }

    return domain::Result<void>::success();
}

ManifestStore::ManifestStore(std::shared_ptr<IAtomicFileWriter> writer) : writer_(std::move(writer))
{
    if (!writer_)
    {
        writer_ = std::make_shared<QSaveFileWriter>();
    }
}

domain::Result<ProjectManifest> ManifestStore::read(const QString& path) const
{
    const QFileInfo information(path);
    if (!information.exists() || !information.isFile() || information.isSymLink())
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestReadFailed,
            QStringLiteral("Corax could not read an ordinary project manifest file."),
            QStringLiteral(
                "The manifest is missing, is not a regular file, or is a symbolic link."),
            path,
            QStringLiteral(
                "Restore corax.project.json as a regular file inside the project directory.")));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestReadFailed,
            QStringLiteral("Corax could not read the project manifest."),
            file.errorString(),
            path,
            QStringLiteral("Check that this is a readable Corax project directory.")));
    }

    if (file.size() > kMaximumManifestBytes)
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project manifest is too large."),
                          QStringLiteral("The manifest exceeds the 1 MiB safety limit."),
                          path));
    }

    const QByteArray contents = file.read(kMaximumManifestBytes + 1);
    if (file.error() != QFileDevice::NoError)
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestReadFailed,
                          QStringLiteral("Corax could not finish reading the project manifest."),
                          file.errorString(),
                          path));
    }
    if (contents.size() > kMaximumManifestBytes)
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project manifest is too large."),
                          QStringLiteral("The manifest exceeds the 1 MiB safety limit."),
                          path));
    }

    return parse(contents, path);
}

domain::Result<ProjectManifest> ManifestStore::parse(const QByteArray& contents,
                                                     const QString& sourcePath) const
{
    if (contents.startsWith("\xEF\xBB\xBF"))
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project manifest encoding is not valid."),
                          QStringLiteral("UTF-8 byte-order marks are not permitted."),
                          sourcePath));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project manifest contains invalid JSON."),
                          QStringLiteral("JSON parse error at byte %1: %2")
                              .arg(parseError.offset)
                              .arg(parseError.errorString()),
                          sourcePath));
    }

    const QJsonObject object = document.object();
    for (const QString& key : {
             QStringLiteral("format"),
             QStringLiteral("projectId"),
             QStringLiteral("displayName"),
             QStringLiteral("database"),
             QStringLiteral("createdAt"),
             QStringLiteral("minimumCoraxVersion"),
         })
    {
        auto required = requireString(object, key, sourcePath);
        if (!required)
        {
            return domain::Result<ProjectManifest>::failure(std::move(required).error());
        }
    }

    if (!object.contains(QStringLiteral("formatVersion")) ||
        !object.value(QStringLiteral("formatVersion")).isDouble())
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("The project manifest is not valid."),
            QStringLiteral("Required field 'formatVersion' is missing or is not an integer."),
            sourcePath));
    }

    const double rawVersion = object.value(QStringLiteral("formatVersion")).toDouble();
    const int formatVersion = object.value(QStringLiteral("formatVersion")).toInt(-1);
    if (rawVersion != static_cast<double>(formatVersion) || formatVersion < 1)
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project manifest version is not valid."),
                          QStringLiteral("formatVersion must be a positive integer."),
                          sourcePath));
    }
    if (formatVersion > kCurrentManifestVersion)
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestVersionUnsupported,
            QStringLiteral("This project needs a newer Corax version."),
            QStringLiteral("Manifest format version %1 is newer than supported version %2.")
                .arg(formatVersion)
                .arg(kCurrentManifestVersion),
            sourcePath,
            QStringLiteral("Update Corax before opening this project.")));
    }

    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("org.corax.project"))
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("This directory is not a Corax project."),
            QStringLiteral("The manifest format identifier is not 'org.corax.project'."),
            sourcePath));
    }

    const QString projectIdText = object.value(QStringLiteral("projectId")).toString();
    const QUuid projectId(projectIdText);
    if (projectId.isNull() || projectIdText != projectId.toString(QUuid::WithoutBraces))
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("The project manifest has an invalid project ID."),
            QStringLiteral("projectId must be a canonical lowercase UUID without braces."),
            sourcePath));
    }

    const QString displayName = object.value(QStringLiteral("displayName")).toString();
    if (displayName.trimmed().isEmpty() || displayName.size() > 256)
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project manifest has an invalid display name."),
                          QStringLiteral("displayName must contain 1 to 256 Unicode code units."),
                          sourcePath));
    }

    const QString databaseFile = object.value(QStringLiteral("database")).toString();
    if (databaseFile != QString::fromLatin1(kDatabaseFileName))
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("The project database path is not supported."),
            QStringLiteral("database must be the project-relative path 'project.sqlite3'."),
            sourcePath));
    }

    const QString createdAtText = object.value(QStringLiteral("createdAt")).toString();
    QDateTime createdAt = QDateTime::fromString(createdAtText, Qt::ISODateWithMs);
    if (!createdAt.isValid())
    {
        createdAt = QDateTime::fromString(createdAtText, Qt::ISODate);
    }
    if (!createdAt.isValid() || createdAt.timeSpec() == Qt::LocalTime)
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("The project creation time is not valid."),
            QStringLiteral("createdAt must be an ISO 8601 timestamp with a UTC or numeric offset."),
            sourcePath));
    }

    const QString minimumVersion = object.value(QStringLiteral("minimumCoraxVersion")).toString();
    QVersionNumber parsedMinimumVersion;
    if (!parseCanonicalVersion(minimumVersion, parsedMinimumVersion))
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestInvalid,
            QStringLiteral("The project compatibility version is not valid."),
            QStringLiteral("minimumCoraxVersion must be a canonical three-part numeric version."),
            sourcePath));
    }
    const QVersionNumber applicationVersion = currentCoraxVersion();
    if (parsedMinimumVersion > applicationVersion)
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestVersionUnsupported,
                          QStringLiteral("This project needs a newer Corax version."),
                          QStringLiteral("minimumCoraxVersion is %1; this build is %2.")
                              .arg(minimumVersion, applicationVersion.toString()),
                          sourcePath,
                          QStringLiteral("Update Corax before opening this project.")));
    }

    if (object.contains(QStringLiteral("sourceRoots")) &&
        !object.value(QStringLiteral("sourceRoots")).isArray())
    {
        return domain::Result<ProjectManifest>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("The project source-root list is not valid."),
                          QStringLiteral("sourceRoots must be a JSON array when present."),
                          sourcePath));
    }
    if (!object.value(QStringLiteral("sourceRoots")).toArray().isEmpty())
    {
        return domain::Result<ProjectManifest>::failure(manifestError(
            domain::ErrorCode::ManifestFeatureUnsupported,
            QStringLiteral(
                "This project contains source roots that this Corax milestone cannot validate."),
            QStringLiteral("Milestone 0 accepts only an absent or empty sourceRoots array."),
            sourcePath,
            QStringLiteral(
                "Open this project with a Corax version that supports source-root bindings.")));
    }

    return domain::Result<ProjectManifest>::success({
        .projectId = projectId,
        .displayName = displayName,
        .databaseFile = databaseFile,
        .createdAtUtc = createdAt.toUTC(),
        .minimumCoraxVersion = minimumVersion,
        .preservedFields = object,
    });
}

QByteArray ManifestStore::serialize(const ProjectManifest& manifest) const
{
    QJsonObject object = manifest.preservedFields;
    object.insert(QStringLiteral("format"), QStringLiteral("org.corax.project"));
    object.insert(QStringLiteral("formatVersion"), kCurrentManifestVersion);
    object.insert(QStringLiteral("projectId"), manifest.projectId.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("displayName"), manifest.displayName);
    object.insert(QStringLiteral("database"), manifest.databaseFile);
    object.insert(QStringLiteral("createdAt"),
                  manifest.createdAtUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("minimumCoraxVersion"), manifest.minimumCoraxVersion);
    if (!object.contains(QStringLiteral("sourceRoots")))
    {
        object.insert(QStringLiteral("sourceRoots"), QJsonArray{});
    }

    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!bytes.endsWith('\n'))
    {
        bytes.append('\n');
    }
    return bytes;
}

domain::Result<void> ManifestStore::write(const QString& path,
                                          const ProjectManifest& manifest) const
{
    if (!manifest.isValid())
    {
        return domain::Result<void>::failure(
            manifestError(domain::ErrorCode::ManifestInvalid,
                          QStringLiteral("Corax cannot write an invalid project manifest."),
                          QStringLiteral("The in-memory manifest failed invariant validation."),
                          path));
    }
    return writer_->writeAtomically(path, serialize(manifest));
}

} // namespace corax::storage_sqlite
