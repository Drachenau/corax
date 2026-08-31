// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/domain/ProjectInfo.h"
#include "corax/domain/Result.h"

#include <optional>

namespace corax::application
{

struct NewProject final
{
    QUuid projectId;
    QString displayName;
    QString projectPath;
    QDateTime createdAtUtc;
};

class IProjectStore
{
public:
    virtual ~IProjectStore() = default;

    [[nodiscard]] virtual domain::Result<domain::ProjectInfo>
    createProject(const NewProject& project) = 0;
    [[nodiscard]] virtual domain::Result<domain::ProjectInfo>
    openProject(const QString& projectDirectory) = 0;
    [[nodiscard]] virtual domain::Result<void> closeProject() = 0;
    [[nodiscard]] virtual std::optional<domain::ProjectInfo> currentProject() const = 0;
};

} // namespace corax::application
