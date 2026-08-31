// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace corax::domain
{

struct ProjectInfo final
{
    QUuid projectId;
    QString displayName;
    QString projectPath;
    QDateTime createdAtUtc;
    qint64 revision{0};

    [[nodiscard]] bool isValid() const
    {
        return !projectId.isNull() && !displayName.trimmed().isEmpty() && !projectPath.isEmpty() &&
               createdAtUtc.isValid() && revision >= 0;
    }

    friend bool operator==(const ProjectInfo&, const ProjectInfo&) = default;
};

} // namespace corax::domain
