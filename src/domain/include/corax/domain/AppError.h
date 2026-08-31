// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <QString>

namespace corax::domain
{

// Stable application-facing error identifiers. Add new values at the end and
// do not reuse an identifier for a different condition.
enum class ErrorCode
{
    InvalidArgument,
    ProjectAlreadyOpen,
    ProjectNotOpen,
    ProjectPathInvalid,
    ProjectAlreadyExists,
    ProjectCreateFailed,
    ManifestReadFailed,
    ManifestInvalid,
    ManifestVersionUnsupported,
    ManifestFeatureUnsupported,
    ManifestWriteFailed,
    DatabaseOpenFailed,
    DatabaseFeatureMissing,
    DatabaseInitializeFailed,
    DatabaseSchemaUnsupported,
    DatabaseMigrationFailed,
    DatabaseIntegrityFailed,
    ProjectIdentityMismatch,
    ProjectLocked,
    ProjectLockFailed,
    ProjectLockOwnershipLost,
    TransactionFailed,
    InternalError,
};

[[nodiscard]] QString errorCodeName(ErrorCode code);

struct AppError final
{
    ErrorCode code{ErrorCode::InternalError};
    QString userMessage;
    QString technicalContext;
    QString remediation;
    QString affectedPath;
    bool retryable{false};

    [[nodiscard]] QString stableCode() const
    {
        return errorCodeName(code);
    }
};

} // namespace corax::domain
