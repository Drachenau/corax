// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/application/IProjectStore.h"

#include <functional>

namespace corax::application
{

class ProjectService final
{
public:
    using IdGenerator = std::function<QUuid()>;
    using Clock = std::function<QDateTime()>;

    explicit ProjectService(IProjectStore& store);
    ProjectService(IProjectStore& store, IdGenerator idGenerator, Clock clock);

    [[nodiscard]] domain::Result<domain::ProjectInfo> createProject(const QString& projectDirectory,
                                                                    const QString& displayName);
    [[nodiscard]] domain::Result<domain::ProjectInfo> openProject(const QString& projectDirectory);
    [[nodiscard]] domain::Result<void> closeProject();
    [[nodiscard]] std::optional<domain::ProjectInfo> currentProject() const;

private:
    IProjectStore& store_;
    IdGenerator idGenerator_;
    Clock clock_;
};

} // namespace corax::application
