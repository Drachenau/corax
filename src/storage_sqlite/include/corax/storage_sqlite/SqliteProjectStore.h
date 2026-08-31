// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/application/IProjectStore.h"
#include "corax/storage_sqlite/ProjectManifest.h"

#include <functional>
#include <memory>

namespace corax::storage_sqlite
{

class SqliteProjectStore final : public application::IProjectStore
{
public:
    // Test synchronization seam after the initial path check and before this
    // operation creates or locks the project directory. Production composition
    // leaves this empty.
    using InitialPathCheckpoint = std::function<void()>;

    explicit SqliteProjectStore(std::shared_ptr<IAtomicFileWriter> manifestWriter = {});
    SqliteProjectStore(std::shared_ptr<IAtomicFileWriter> manifestWriter,
                       InitialPathCheckpoint initialPathCheckpoint);
    ~SqliteProjectStore() override;

    SqliteProjectStore(const SqliteProjectStore&) = delete;
    SqliteProjectStore& operator=(const SqliteProjectStore&) = delete;
    SqliteProjectStore(SqliteProjectStore&&) = delete;
    SqliteProjectStore& operator=(SqliteProjectStore&&) = delete;

    [[nodiscard]] domain::Result<domain::ProjectInfo>
    createProject(const application::NewProject& project) override;
    [[nodiscard]] domain::Result<domain::ProjectInfo>
    openProject(const QString& projectDirectory) override;
    [[nodiscard]] domain::Result<void> closeProject() override;
    [[nodiscard]] std::optional<domain::ProjectInfo> currentProject() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace corax::storage_sqlite
