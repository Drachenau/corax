// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/domain/ProjectInfo.h"
#include "corax/domain/Result.h"

#include <QStringList>
#include <QVector>

#include <memory>

namespace corax::storage_sqlite
{

inline constexpr int kCoraxSqliteApplicationId = 0x434F5258; // ASCII "CORX"
inline constexpr int kCurrentDatabaseSchemaVersion = 1;
inline constexpr auto kPinnedSqliteVersion = "3.53.4";

struct MigrationRecord final
{
    int sequence{0};
    QString migrationId;
    QString checksum;
    QString applicationVersion;
    QDateTime startedAtUtc;
    QDateTime completedAtUtc;
    QString result;
};

struct SqliteRuntimeInfo final
{
    QString version;
    QStringList compileOptions;
    bool fts5Available{false};
    bool threadSafe{false};
};

class ISqliteInitializationFaultInjector
{
public:
    virtual ~ISqliteInitializationFaultInjector() = default;
    [[nodiscard]] virtual domain::Result<void> beforeInitialMigrationCommit() = 0;
};

class SqliteProjectDatabase final
{
public:
    ~SqliteProjectDatabase();

    SqliteProjectDatabase(const SqliteProjectDatabase&) = delete;
    SqliteProjectDatabase& operator=(const SqliteProjectDatabase&) = delete;
    SqliteProjectDatabase(SqliteProjectDatabase&&) = delete;
    SqliteProjectDatabase& operator=(SqliteProjectDatabase&&) = delete;

    [[nodiscard]] static domain::Result<std::unique_ptr<SqliteProjectDatabase>>
    initializeNew(const QString& databasePath,
                  const domain::ProjectInfo& project,
                  ISqliteInitializationFaultInjector* faultInjector = nullptr);

    [[nodiscard]] static domain::Result<std::unique_ptr<SqliteProjectDatabase>>
    openExisting(const QString& databasePath,
                 const QString& projectDirectory,
                 const QUuid& expectedProjectId);

    [[nodiscard]] domain::Result<domain::ProjectInfo> projectInfo() const;
    [[nodiscard]] domain::Result<QVector<MigrationRecord>> migrationLedger() const;
    [[nodiscard]] SqliteRuntimeInfo runtimeInfo() const;

    [[nodiscard]] static QString initialMigrationId();
    [[nodiscard]] static QString initialMigrationChecksum();

private:
    class Impl;
    explicit SqliteProjectDatabase(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace corax::storage_sqlite
