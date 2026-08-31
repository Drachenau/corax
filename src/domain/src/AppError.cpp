// SPDX-License-Identifier: Apache-2.0

#include "corax/domain/AppError.h"

namespace corax::domain
{

QString errorCodeName(const ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::InvalidArgument:
        return QStringLiteral("core.invalid_argument");
    case ErrorCode::ProjectAlreadyOpen:
        return QStringLiteral("project.already_open");
    case ErrorCode::ProjectNotOpen:
        return QStringLiteral("project.not_open");
    case ErrorCode::ProjectPathInvalid:
        return QStringLiteral("project.path_invalid");
    case ErrorCode::ProjectAlreadyExists:
        return QStringLiteral("project.already_exists");
    case ErrorCode::ProjectCreateFailed:
        return QStringLiteral("project.create_failed");
    case ErrorCode::ManifestReadFailed:
        return QStringLiteral("manifest.read_failed");
    case ErrorCode::ManifestInvalid:
        return QStringLiteral("manifest.invalid");
    case ErrorCode::ManifestVersionUnsupported:
        return QStringLiteral("manifest.version_unsupported");
    case ErrorCode::ManifestFeatureUnsupported:
        return QStringLiteral("manifest.feature_unsupported");
    case ErrorCode::ManifestWriteFailed:
        return QStringLiteral("manifest.write_failed");
    case ErrorCode::DatabaseOpenFailed:
        return QStringLiteral("database.open_failed");
    case ErrorCode::DatabaseFeatureMissing:
        return QStringLiteral("database.feature_missing");
    case ErrorCode::DatabaseInitializeFailed:
        return QStringLiteral("database.initialize_failed");
    case ErrorCode::DatabaseSchemaUnsupported:
        return QStringLiteral("database.schema_unsupported");
    case ErrorCode::DatabaseMigrationFailed:
        return QStringLiteral("database.migration_failed");
    case ErrorCode::DatabaseIntegrityFailed:
        return QStringLiteral("database.integrity_failed");
    case ErrorCode::ProjectIdentityMismatch:
        return QStringLiteral("project.identity_mismatch");
    case ErrorCode::ProjectLocked:
        return QStringLiteral("project.locked");
    case ErrorCode::ProjectLockFailed:
        return QStringLiteral("project.lock_failed");
    case ErrorCode::ProjectLockOwnershipLost:
        return QStringLiteral("project.lock_ownership_lost");
    case ErrorCode::TransactionFailed:
        return QStringLiteral("database.transaction_failed");
    case ErrorCode::InternalError:
        return QStringLiteral("core.internal_error");
    }

    return QStringLiteral("core.unknown_error");
}

} // namespace corax::domain
