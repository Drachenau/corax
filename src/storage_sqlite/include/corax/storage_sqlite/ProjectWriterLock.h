// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/domain/Result.h"

#include <QString>
#include <QUuid>

#include <optional>

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
    [[nodiscard]] domain::Result<void> verifyOwnership();
    [[nodiscard]] domain::Result<void> release();
    void abandon() noexcept;

    [[nodiscard]] bool ownsLock() const noexcept
    {
        return state_ == State::Owned;
    }
    [[nodiscard]] bool recoveryRequired() const noexcept
    {
        return state_ == State::OwnershipLost;
    }
    [[nodiscard]] QString lockPath() const
    {
        return lockPath_;
    }
    [[nodiscard]] std::optional<domain::AppError> recoveryError() const;

private:
    enum class State
    {
        Unowned,
        Owned,
        OwnershipLost,
    };

    void markOwnershipLost(domain::AppError error);
    void clearLocalState() noexcept;

    State state_{State::Unowned};
    QString lockPath_;
    QString ownershipToken_;
    QUuid projectId_;
    std::optional<domain::AppError> recoveryError_;
};

} // namespace corax::storage_sqlite
