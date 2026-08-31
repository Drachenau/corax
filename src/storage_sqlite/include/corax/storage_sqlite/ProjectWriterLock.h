// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/domain/Result.h"

#include <QString>
#include <QUuid>

namespace corax::storage_sqlite
{

inline constexpr auto kWriterLockFileName = ".corax.writer.lock";

class ProjectWriterLock final
{
public:
    ProjectWriterLock() = default;
    ~ProjectWriterLock();

    ProjectWriterLock(const ProjectWriterLock&) = delete;
    ProjectWriterLock& operator=(const ProjectWriterLock&) = delete;
    ProjectWriterLock(ProjectWriterLock&&) = delete;
    ProjectWriterLock& operator=(ProjectWriterLock&&) = delete;

    [[nodiscard]] domain::Result<void> acquire(const QString& projectDirectory,
                                               const QUuid& projectId);
    [[nodiscard]] domain::Result<void> release();

    [[nodiscard]] bool ownsLock() const noexcept
    {
        return !ownershipToken_.isEmpty();
    }
    [[nodiscard]] QString lockPath() const
    {
        return lockPath_;
    }

private:
    QString lockPath_;
    QString ownershipToken_;
    QUuid projectId_;
};

} // namespace corax::storage_sqlite
