// SPDX-License-Identifier: Apache-2.0

#include "corax/storage_sqlite/SqliteProjectDatabase.h"

#include "corax/domain/AppError.h"

#include "sqlite3.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace corax::storage_sqlite
{
namespace
{

constexpr auto kInitialMigrationId = "0001_empty_project";
constexpr auto kApplicationVersion = "0.1.0";

constexpr auto kInitialMigrationSql = R"sql(
CREATE TABLE schema_migrations (
    sequence INTEGER PRIMARY KEY NOT NULL CHECK (sequence > 0),
    migration_id TEXT UNIQUE NOT NULL,
    checksum TEXT NOT NULL,
    application_version TEXT NOT NULL,
    started_at_utc TEXT NOT NULL,
    completed_at_utc TEXT NOT NULL,
    result TEXT NOT NULL CHECK (result = 'applied')
) STRICT;
CREATE TABLE project_metadata (
    singleton INTEGER PRIMARY KEY NOT NULL CHECK (singleton = 1),
    project_id TEXT NOT NULL,
    display_name TEXT NOT NULL CHECK (length(display_name) BETWEEN 1 AND 256),
    created_at_utc TEXT NOT NULL,
    modified_at_utc TEXT NOT NULL,
    schema_version INTEGER NOT NULL CHECK (schema_version > 0),
    project_revision INTEGER NOT NULL DEFAULT 0 CHECK (project_revision >= 0),
    minimum_corax_version TEXT NOT NULL
) STRICT;
)sql";

domain::AppError databaseError(const domain::ErrorCode code,
                               QString userMessage,
                               QString technicalContext,
                               const QString& path,
                               QString remediation,
                               const bool retryable = false)
{
    return {
        .code = code,
        .userMessage = std::move(userMessage),
        .technicalContext = std::move(technicalContext),
        .remediation = std::move(remediation),
        .affectedPath = path,
        .retryable = retryable,
    };
}

domain::AppError sqliteFailure(sqlite3* database,
                               const int resultCode,
                               const QString& path,
                               const QString& operation,
                               const domain::ErrorCode code = domain::ErrorCode::DatabaseOpenFailed)
{
    const QString message = database != nullptr ? QString::fromUtf8(sqlite3_errmsg(database))
                                                : QString::fromUtf8(sqlite3_errstr(resultCode));
    return databaseError(
        code,
        QStringLiteral("Corax could not use the project database."),
        QStringLiteral("%1 failed with SQLite code %2: %3")
            .arg(operation)
            .arg(resultCode)
            .arg(message),
        path,
        QStringLiteral("Check that the project is writable and not damaged, then try again."),
        resultCode == SQLITE_BUSY || resultCode == SQLITE_LOCKED);
}

class Statement final
{
public:
    Statement() = default;
    ~Statement()
    {
        if (statement_ != nullptr)
        {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : statement_(std::exchange(other.statement_, nullptr)) {}

    Statement& operator=(Statement&& other) noexcept
    {
        if (this != &other)
        {
            if (statement_ != nullptr)
            {
                sqlite3_finalize(statement_);
            }
            statement_ = std::exchange(other.statement_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] sqlite3_stmt** out()
    {
        return &statement_;
    }
    [[nodiscard]] sqlite3_stmt* get() const
    {
        return statement_;
    }

private:
    sqlite3_stmt* statement_{nullptr};
};

class Connection final
{
public:
    explicit Connection(QString path) : path_(std::move(path)) {}

    ~Connection()
    {
        if (database_ != nullptr)
        {
            sqlite3_close_v2(database_);
        }
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    [[nodiscard]] domain::Result<void> open(const bool create)
    {
        const QByteArray utf8Path = path_.toUtf8();
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_URI;
        if (create)
        {
            flags |= SQLITE_OPEN_CREATE;
        }
        const int code = sqlite3_open_v2(utf8Path.constData(), &database_, flags, nullptr);
        if (code != SQLITE_OK)
        {
            return domain::Result<void>::failure(
                sqliteFailure(database_, code, path_, QStringLiteral("sqlite3_open_v2")));
        }
        return domain::Result<void>::success();
    }

    [[nodiscard]] domain::Result<void>
    execute(const char* sql,
            const QString& operation,
            const domain::ErrorCode code = domain::ErrorCode::DatabaseOpenFailed)
    {
        char* rawMessage = nullptr;
        const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &rawMessage);
        if (result != SQLITE_OK)
        {
            const QString message =
                rawMessage != nullptr ? QString::fromUtf8(rawMessage) : QString{};
            sqlite3_free(rawMessage);
            auto error = sqliteFailure(database_, result, path_, operation, code);
            if (!message.isEmpty())
            {
                error.technicalContext += QStringLiteral("; sqlite3_exec detail: %1").arg(message);
            }
            return domain::Result<void>::failure(std::move(error));
        }
        return domain::Result<void>::success();
    }

    [[nodiscard]] domain::Result<Statement>
    prepare(const char* sql,
            const QString& operation,
            const domain::ErrorCode code = domain::ErrorCode::DatabaseOpenFailed) const
    {
        Statement statement;
        const int result = sqlite3_prepare_v2(database_, sql, -1, statement.out(), nullptr);
        if (result != SQLITE_OK)
        {
            return domain::Result<Statement>::failure(
                sqliteFailure(database_, result, path_, operation, code));
        }
        return domain::Result<Statement>::success(std::move(statement));
    }

    [[nodiscard]] domain::Result<qint64> scalarInteger(const char* sql,
                                                       const QString& operation) const
    {
        auto prepared = prepare(sql, operation);
        if (!prepared)
        {
            return domain::Result<qint64>::failure(std::move(prepared).error());
        }
        Statement statement = std::move(prepared).value();
        const int step = sqlite3_step(statement.get());
        if (step != SQLITE_ROW)
        {
            return domain::Result<qint64>::failure(
                sqliteFailure(database_, step, path_, operation));
        }
        return domain::Result<qint64>::success(sqlite3_column_int64(statement.get(), 0));
    }

    [[nodiscard]] domain::Result<QString> scalarText(const char* sql,
                                                     const QString& operation) const
    {
        auto prepared = prepare(sql, operation);
        if (!prepared)
        {
            return domain::Result<QString>::failure(std::move(prepared).error());
        }
        Statement statement = std::move(prepared).value();
        const int step = sqlite3_step(statement.get());
        if (step != SQLITE_ROW)
        {
            return domain::Result<QString>::failure(
                sqliteFailure(database_, step, path_, operation));
        }
        const auto* text = sqlite3_column_text(statement.get(), 0);
        return domain::Result<QString>::success(
            text != nullptr ? QString::fromUtf8(reinterpret_cast<const char*>(text)) : QString{});
    }

    [[nodiscard]] sqlite3* get() const
    {
        return database_;
    }
    [[nodiscard]] const QString& path() const
    {
        return path_;
    }

private:
    QString path_;
    sqlite3* database_{nullptr};
};

domain::Result<void> bindText(sqlite3* database,
                              sqlite3_stmt* statement,
                              const int index,
                              const QString& value,
                              const QString& path,
                              const QString& operation)
{
    const QByteArray utf8 = value.toUtf8();
    const sqlite3_uint64 allocationSize = static_cast<sqlite3_uint64>(utf8.size()) + 1U;
    auto* copy = static_cast<char*>(sqlite3_malloc64(allocationSize));
    if (copy == nullptr)
    {
        return domain::Result<void>::failure(databaseError(
            domain::ErrorCode::DatabaseInitializeFailed,
            QStringLiteral("Corax ran out of memory while preparing project metadata."),
            QStringLiteral("sqlite3_malloc64 returned null for %1 bytes.").arg(allocationSize),
            path,
            QStringLiteral("Close other applications and try again."),
            true));
    }
    std::memcpy(copy, utf8.constData(), static_cast<std::size_t>(utf8.size()));
    copy[utf8.size()] = '\0';
    const int result = sqlite3_bind_text64(statement,
                                           index,
                                           copy,
                                           static_cast<sqlite3_uint64>(utf8.size()),
                                           sqlite3_free,
                                           static_cast<unsigned char>(SQLITE_UTF8));
    if (result != SQLITE_OK)
    {
        return domain::Result<void>::failure(sqliteFailure(
            database, result, path, operation, domain::ErrorCode::DatabaseInitializeFailed));
    }
    return domain::Result<void>::success();
}

domain::Result<void> stepDone(sqlite3* database,
                              sqlite3_stmt* statement,
                              const QString& path,
                              const QString& operation,
                              const domain::ErrorCode code)
{
    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE)
    {
        return domain::Result<void>::failure(
            sqliteFailure(database, result, path, operation, code));
    }
    return domain::Result<void>::success();
}

QString checksumForInitialMigration()
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayView(kInitialMigrationSql), QCryptographicHash::Sha256)
            .toHex());
}

SqliteRuntimeInfo collectRuntimeInfo()
{
    SqliteRuntimeInfo info;
    info.version = QString::fromLatin1(sqlite3_libversion());
    info.fts5Available = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
    info.threadSafe = sqlite3_threadsafe() != 0;
    for (int index = 0;; ++index)
    {
        const char* option = sqlite3_compileoption_get(index);
        if (option == nullptr)
        {
            break;
        }
        info.compileOptions.append(QString::fromLatin1(option));
    }
    return info;
}

domain::Result<void> verifyRuntimeFeatures(const QString& path)
{
    const std::array requiredOptions{
        "ENABLE_FTS5",
        "THREADSAFE=1",
        "DQS=0",
        "OMIT_LOAD_EXTENSION",
        "DEFAULT_FOREIGN_KEYS",
    };
    QStringList missing;
    for (const char* option : requiredOptions)
    {
        if (sqlite3_compileoption_used(option) == 0)
        {
            missing.append(QString::fromLatin1(option));
        }
    }
    if (QString::fromLatin1(sqlite3_libversion()) != QString::fromLatin1(kPinnedSqliteVersion))
    {
        missing.append(QStringLiteral("SQLite version %1 (runtime is %2)")
                           .arg(QString::fromLatin1(kPinnedSqliteVersion),
                                QString::fromLatin1(sqlite3_libversion())));
    }
    if (sqlite3_threadsafe() == 0)
    {
        missing.append(QStringLiteral("runtime thread safety"));
    }
    if (!missing.isEmpty())
    {
        return domain::Result<void>::failure(databaseError(
            domain::ErrorCode::DatabaseFeatureMissing,
            QStringLiteral("This Corax build is missing required database features."),
            QStringLiteral("Missing or incompatible SQLite features: %1").arg(missing.join(", ")),
            path,
            QStringLiteral("Install a Corax build that contains the pinned SQLite dependency.")));
    }
    return domain::Result<void>::success();
}

domain::Result<void> verifyFts5Operational(Connection& connection)
{
    auto create =
        connection.execute("CREATE VIRTUAL TABLE temp.corax_fts5_probe USING fts5(value);",
                           QStringLiteral("verify FTS5 virtual table"),
                           domain::ErrorCode::DatabaseFeatureMissing);
    if (!create)
    {
        return create;
    }
    return connection.execute("DROP TABLE temp.corax_fts5_probe;",
                              QStringLiteral("remove FTS5 verification table"),
                              domain::ErrorCode::DatabaseFeatureMissing);
}

domain::Result<void> applyConnectionPolicy(Connection& connection)
{
    for (const auto& [sql, operation] : std::array{
             std::pair{"PRAGMA busy_timeout = 2000;", "set busy timeout"},
             std::pair{"PRAGMA foreign_keys = ON;", "enable foreign keys"},
             std::pair{"PRAGMA synchronous = NORMAL;", "set synchronous mode"},
             std::pair{"PRAGMA secure_delete = FAST;", "set secure delete"},
         })
    {
        auto result = connection.execute(sql, QString::fromLatin1(operation));
        if (!result)
        {
            return result;
        }
    }

    auto journalMode = connection.scalarText("PRAGMA journal_mode = WAL;",
                                             QStringLiteral("enable WAL journal mode"));
    if (!journalMode)
    {
        return domain::Result<void>::failure(std::move(journalMode).error());
    }
    if (journalMode.value().compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0)
    {
        return domain::Result<void>::failure(databaseError(
            domain::ErrorCode::DatabaseOpenFailed,
            QStringLiteral("The project database cannot use WAL journal mode."),
            QStringLiteral("SQLite returned journal_mode=%1.").arg(journalMode.value()),
            connection.path(),
            QStringLiteral("Move the project to a local filesystem with SQLite WAL support.")));
    }
    return domain::Result<void>::success();
}

domain::Result<void> verifyPersistentPolicy(Connection& connection)
{
    struct IntegerPragma final
    {
        const char* sql;
        qint64 expected;
        const char* name;
    };
    const std::array expected{
        IntegerPragma{"PRAGMA page_size;", 4096, "page_size"},
        IntegerPragma{"PRAGMA auto_vacuum;", 2, "auto_vacuum"},
        IntegerPragma{"PRAGMA foreign_keys;", 1, "foreign_keys"},
        IntegerPragma{"PRAGMA synchronous;", 1, "synchronous"},
        IntegerPragma{"PRAGMA secure_delete;", 2, "secure_delete"},
    };
    for (const auto& pragma : expected)
    {
        auto value =
            connection.scalarInteger(pragma.sql, QStringLiteral("verify %1").arg(pragma.name));
        if (!value)
        {
            return domain::Result<void>::failure(std::move(value).error());
        }
        if (value.value() != pragma.expected)
        {
            return domain::Result<void>::failure(databaseError(
                domain::ErrorCode::DatabaseOpenFailed,
                QStringLiteral("The project database policy is not active."),
                QStringLiteral("PRAGMA %1 returned %2; expected %3.")
                    .arg(QString::fromLatin1(pragma.name))
                    .arg(value.value())
                    .arg(pragma.expected),
                connection.path(),
                QStringLiteral("Close other database tools and reopen the project.")));
        }
    }
    return domain::Result<void>::success();
}

domain::Result<void> verifyDatabaseIdentityAndVersion(Connection& connection)
{
    auto applicationId = connection.scalarInteger("PRAGMA application_id;",
                                                  QStringLiteral("read SQLite application ID"));
    if (!applicationId)
    {
        return domain::Result<void>::failure(std::move(applicationId).error());
    }
    if (applicationId.value() != kCoraxSqliteApplicationId)
    {
        return domain::Result<void>::failure(databaseError(
            domain::ErrorCode::DatabaseSchemaUnsupported,
            QStringLiteral("This file is not a Corax project database."),
            QStringLiteral("SQLite application_id is %1; expected %2.")
                .arg(applicationId.value())
                .arg(kCoraxSqliteApplicationId),
            connection.path(),
            QStringLiteral(
                "Choose the project directory that contains the matching Corax database.")));
    }

    auto userVersion = connection.scalarInteger("PRAGMA user_version;",
                                                QStringLiteral("read SQLite schema version"));
    if (!userVersion)
    {
        return domain::Result<void>::failure(std::move(userVersion).error());
    }
    if (userVersion.value() != kCurrentDatabaseSchemaVersion)
    {
        return domain::Result<void>::failure(databaseError(
            domain::ErrorCode::DatabaseSchemaUnsupported,
            userVersion.value() > kCurrentDatabaseSchemaVersion
                ? QStringLiteral("This project needs a newer Corax version.")
                : QStringLiteral("This project database schema is not supported."),
            QStringLiteral("SQLite user_version is %1; supported version is %2.")
                .arg(userVersion.value())
                .arg(kCurrentDatabaseSchemaVersion),
            connection.path(),
            userVersion.value() > kCurrentDatabaseSchemaVersion
                ? QStringLiteral("Update Corax before opening this project.")
                : QStringLiteral(
                      "Restore a valid Corax project backup or use a supported migration path.")));
    }
    return domain::Result<void>::success();
}

domain::Result<void> verifyIntegrity(Connection& connection)
{
    auto check =
        connection.scalarText("PRAGMA quick_check(1);", QStringLiteral("run SQLite quick_check"));
    if (!check)
    {
        return domain::Result<void>::failure(std::move(check).error());
    }
    if (check.value() != QStringLiteral("ok"))
    {
        return domain::Result<void>::failure(
            databaseError(domain::ErrorCode::DatabaseIntegrityFailed,
                          QStringLiteral("The project database did not pass its integrity check."),
                          QStringLiteral("SQLite quick_check returned: %1").arg(check.value()),
                          connection.path(),
                          QStringLiteral("Do not modify the project. Restore a backup or use a "
                                         "documented repair process.")));
    }
    return domain::Result<void>::success();
}

domain::Result<void> insertInitialMetadata(Connection& connection,
                                           const domain::ProjectInfo& project)
{
    auto prepared =
        connection.prepare("INSERT INTO project_metadata "
                           "(singleton, project_id, display_name, created_at_utc, modified_at_utc, "
                           " schema_version, project_revision, minimum_corax_version) "
                           "VALUES (1, ?1, ?2, ?3, ?3, 1, 0, ?4);",
                           QStringLiteral("prepare initial project metadata"),
                           domain::ErrorCode::DatabaseInitializeFailed);
    if (!prepared)
    {
        return domain::Result<void>::failure(std::move(prepared).error());
    }
    Statement statement = std::move(prepared).value();
    const QString createdAt = project.createdAtUtc.toUTC().toString(Qt::ISODateWithMs);
    const std::array values{
        project.projectId.toString(QUuid::WithoutBraces),
        project.displayName,
        createdAt,
        QString::fromLatin1(kApplicationVersion),
    };
    for (int index = 0; index < static_cast<int>(values.size()); ++index)
    {
        auto bound = bindText(connection.get(),
                              statement.get(),
                              index + 1,
                              values[static_cast<std::size_t>(index)],
                              connection.path(),
                              QStringLiteral("bind initial project metadata"));
        if (!bound)
        {
            return bound;
        }
    }
    return stepDone(connection.get(),
                    statement.get(),
                    connection.path(),
                    QStringLiteral("insert initial project metadata"),
                    domain::ErrorCode::DatabaseInitializeFailed);
}

domain::Result<void> insertInitialMigrationRecord(Connection& connection,
                                                  const QDateTime& timestamp)
{
    auto prepared = connection.prepare("INSERT INTO schema_migrations "
                                       "(sequence, migration_id, checksum, application_version, "
                                       "started_at_utc, completed_at_utc, result) "
                                       "VALUES (1, ?1, ?2, ?3, ?4, ?4, 'applied');",
                                       QStringLiteral("prepare initial migration ledger record"),
                                       domain::ErrorCode::DatabaseMigrationFailed);
    if (!prepared)
    {
        return domain::Result<void>::failure(std::move(prepared).error());
    }
    Statement statement = std::move(prepared).value();
    const std::array values{
        QString::fromLatin1(kInitialMigrationId),
        checksumForInitialMigration(),
        QString::fromLatin1(kApplicationVersion),
        timestamp.toUTC().toString(Qt::ISODateWithMs),
    };
    for (int index = 0; index < static_cast<int>(values.size()); ++index)
    {
        auto bound = bindText(connection.get(),
                              statement.get(),
                              index + 1,
                              values[static_cast<std::size_t>(index)],
                              connection.path(),
                              QStringLiteral("bind initial migration ledger record"));
        if (!bound)
        {
            return bound;
        }
    }
    return stepDone(connection.get(),
                    statement.get(),
                    connection.path(),
                    QStringLiteral("insert initial migration ledger record"),
                    domain::ErrorCode::DatabaseMigrationFailed);
}

domain::Result<void> initializeSchema(Connection& connection,
                                      const domain::ProjectInfo& project,
                                      ISqliteInitializationFaultInjector* faultInjector)
{
    for (const auto& [sql, operation] : std::array{
             std::pair{"PRAGMA page_size = 4096;", "set database page size"},
             std::pair{"PRAGMA auto_vacuum = INCREMENTAL;", "set incremental auto vacuum"},
         })
    {
        auto result = connection.execute(
            sql, QString::fromLatin1(operation), domain::ErrorCode::DatabaseInitializeFailed);
        if (!result)
        {
            return result;
        }
    }

    auto begin = connection.execute("BEGIN IMMEDIATE;",
                                    QStringLiteral("begin initial schema transaction"),
                                    domain::ErrorCode::TransactionFailed);
    if (!begin)
    {
        return begin;
    }

    auto rollbackWith = [&connection](domain::AppError error)
    {
        static_cast<void>(connection.execute(
            "ROLLBACK;", QStringLiteral("roll back initial schema transaction")));
        return domain::Result<void>::failure(std::move(error));
    };

    auto schema = connection.execute(kInitialMigrationSql,
                                     QStringLiteral("apply initial schema migration"),
                                     domain::ErrorCode::DatabaseMigrationFailed);
    if (!schema)
    {
        return rollbackWith(std::move(schema).error());
    }
    auto metadata = insertInitialMetadata(connection, project);
    if (!metadata)
    {
        return rollbackWith(std::move(metadata).error());
    }
    auto ledger = insertInitialMigrationRecord(connection, project.createdAtUtc);
    if (!ledger)
    {
        return rollbackWith(std::move(ledger).error());
    }
    auto markers =
        connection.execute("PRAGMA application_id = 1129271896; PRAGMA user_version = 1;",
                           QStringLiteral("set Corax database markers"),
                           domain::ErrorCode::DatabaseMigrationFailed);
    if (!markers)
    {
        return rollbackWith(std::move(markers).error());
    }

    if (faultInjector != nullptr)
    {
        auto injected = faultInjector->beforeInitialMigrationCommit();
        if (!injected)
        {
            return rollbackWith(std::move(injected).error());
        }
    }

    auto commit = connection.execute("COMMIT;",
                                     QStringLiteral("commit initial schema transaction"),
                                     domain::ErrorCode::TransactionFailed);
    if (!commit)
    {
        static_cast<void>(connection.execute(
            "ROLLBACK;", QStringLiteral("roll back failed initial schema commit")));
        return commit;
    }
    return domain::Result<void>::success();
}

} // namespace

class SqliteProjectDatabase::Impl final
{
public:
    Impl(std::unique_ptr<Connection> connectionValue, QString projectDirectoryValue)
        : connection(std::move(connectionValue)), projectDirectory(std::move(projectDirectoryValue))
    {
    }

    std::unique_ptr<Connection> connection;
    QString projectDirectory;
};

SqliteProjectDatabase::SqliteProjectDatabase(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SqliteProjectDatabase::~SqliteProjectDatabase() = default;

domain::Result<std::unique_ptr<SqliteProjectDatabase>>
SqliteProjectDatabase::initializeNew(const QString& databasePath,
                                     const domain::ProjectInfo& project,
                                     ISqliteInitializationFaultInjector* faultInjector)
{
    if (!project.isValid())
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            databaseError(domain::ErrorCode::DatabaseInitializeFailed,
                          QStringLiteral("Corax cannot initialize invalid project metadata."),
                          QStringLiteral("ProjectInfo failed invariant validation."),
                          databasePath,
                          QStringLiteral("Choose a valid project directory and display name.")));
    }

    auto features = verifyRuntimeFeatures(databasePath);
    if (!features)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(features).error());
    }

    auto connection = std::make_unique<Connection>(databasePath);
    auto opened = connection->open(true);
    if (!opened)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(opened).error());
    }

    auto existingVersion = connection->scalarInteger(
        "PRAGMA user_version;", QStringLiteral("check uninitialized database"));
    if (!existingVersion)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(existingVersion).error());
    }
    if (existingVersion.value() != 0)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(databaseError(
            domain::ErrorCode::DatabaseInitializeFailed,
            QStringLiteral("The project database is already initialized."),
            QStringLiteral("Expected SQLite user_version 0 but found %1.")
                .arg(existingVersion.value()),
            databasePath,
            QStringLiteral(
                "Choose an empty project directory instead of overwriting this database.")));
    }

    auto initialized = initializeSchema(*connection, project, faultInjector);
    if (!initialized)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(initialized).error());
    }
    auto policy = applyConnectionPolicy(*connection);
    if (!policy)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(policy).error());
    }
    auto persistentPolicy = verifyPersistentPolicy(*connection);
    if (!persistentPolicy)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(persistentPolicy).error());
    }
    auto fts5 = verifyFts5Operational(*connection);
    if (!fts5)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(fts5).error());
    }
    auto integrity = verifyIntegrity(*connection);
    if (!integrity)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(integrity).error());
    }

    auto database = std::unique_ptr<SqliteProjectDatabase>(new SqliteProjectDatabase(
        std::make_unique<Impl>(std::move(connection), project.projectPath)));
    return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::success(std::move(database));
}

domain::Result<std::unique_ptr<SqliteProjectDatabase>> SqliteProjectDatabase::openExisting(
    const QString& databasePath, const QString& projectDirectory, const QUuid& expectedProjectId)
{
    const QFileInfo databaseInformation(databasePath);
    if (!databaseInformation.exists() || !databaseInformation.isFile() ||
        databaseInformation.isSymLink())
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(databaseError(
            domain::ErrorCode::DatabaseOpenFailed,
            QStringLiteral("The project database is missing."),
            QStringLiteral(
                "No ordinary non-symbolic-link file exists at the manifest database path."),
            databasePath,
            QStringLiteral("Restore project.sqlite3 from a backup or choose another project.")));
    }

    auto features = verifyRuntimeFeatures(databasePath);
    if (!features)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(features).error());
    }
    auto connection = std::make_unique<Connection>(databasePath);
    auto opened = connection->open(false);
    if (!opened)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(opened).error());
    }
    auto identity = verifyDatabaseIdentityAndVersion(*connection);
    if (!identity)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(identity).error());
    }
    auto integrity = verifyIntegrity(*connection);
    if (!integrity)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(integrity).error());
    }

    auto database = std::unique_ptr<SqliteProjectDatabase>(
        new SqliteProjectDatabase(std::make_unique<Impl>(std::move(connection), projectDirectory)));

    auto ledger = database->migrationLedger();
    if (!ledger)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(ledger).error());
    }
    if (ledger.value().size() != 1 || ledger.value().front().sequence != 1 ||
        ledger.value().front().migrationId != QString::fromLatin1(kInitialMigrationId) ||
        ledger.value().front().checksum != checksumForInitialMigration() ||
        ledger.value().front().result != QStringLiteral("applied"))
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(databaseError(
            domain::ErrorCode::DatabaseMigrationFailed,
            QStringLiteral("The project migration history is not valid."),
            QStringLiteral(
                "The schema version 1 ledger does not match the immutable initial migration."),
            databasePath,
            QStringLiteral(
                "Restore a known-good backup. Do not edit the migration ledger manually.")));
    }

    auto metadata = database->projectInfo();
    if (!metadata)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(metadata).error());
    }
    if (metadata.value().projectId != expectedProjectId)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(databaseError(
            domain::ErrorCode::ProjectIdentityMismatch,
            QStringLiteral("The project manifest and database do not belong together."),
            QStringLiteral("Manifest projectId=%1, database project_id=%2.")
                .arg(expectedProjectId.toString(QUuid::WithoutBraces),
                     metadata.value().projectId.toString(QUuid::WithoutBraces)),
            projectDirectory,
            QStringLiteral(
                "Restore a matching manifest and database from the same project backup.")));
    }

    auto fts5 = verifyFts5Operational(*database->impl_->connection);
    if (!fts5)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(fts5).error());
    }

    auto policy = applyConnectionPolicy(*database->impl_->connection);
    if (!policy)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(policy).error());
    }
    auto persistentPolicy = verifyPersistentPolicy(*database->impl_->connection);
    if (!persistentPolicy)
    {
        return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::failure(
            std::move(persistentPolicy).error());
    }
    return domain::Result<std::unique_ptr<SqliteProjectDatabase>>::success(std::move(database));
}

domain::Result<domain::ProjectInfo> SqliteProjectDatabase::projectInfo() const
{
    auto prepared = impl_->connection->prepare(
        "SELECT project_id, display_name, created_at_utc, project_revision, schema_version "
        "FROM project_metadata WHERE singleton = 1;",
        QStringLiteral("read project metadata"));
    if (!prepared)
    {
        return domain::Result<domain::ProjectInfo>::failure(std::move(prepared).error());
    }
    Statement statement = std::move(prepared).value();
    const int step = sqlite3_step(statement.get());
    if (step != SQLITE_ROW)
    {
        return domain::Result<domain::ProjectInfo>::failure(
            sqliteFailure(impl_->connection->get(),
                          step,
                          impl_->connection->path(),
                          QStringLiteral("read singleton project metadata"),
                          domain::ErrorCode::DatabaseIntegrityFailed));
    }

    const auto textColumn = [&statement](const int index)
    {
        const auto* value = sqlite3_column_text(statement.get(), index);
        return value != nullptr ? QString::fromUtf8(reinterpret_cast<const char*>(value))
                                : QString{};
    };
    const QString idText = textColumn(0);
    const QUuid projectId(idText);
    QDateTime createdAt = QDateTime::fromString(textColumn(2), Qt::ISODateWithMs);
    if (!createdAt.isValid())
    {
        createdAt = QDateTime::fromString(textColumn(2), Qt::ISODate);
    }
    domain::ProjectInfo project{
        .projectId = projectId,
        .displayName = textColumn(1),
        .projectPath = impl_->projectDirectory,
        .createdAtUtc = createdAt.toUTC(),
        .revision = sqlite3_column_int64(statement.get(), 3),
    };
    const int metadataSchemaVersion = sqlite3_column_int(statement.get(), 4);
    if (metadataSchemaVersion != kCurrentDatabaseSchemaVersion ||
        idText != projectId.toString(QUuid::WithoutBraces) || !project.isValid())
    {
        return domain::Result<domain::ProjectInfo>::failure(databaseError(
            domain::ErrorCode::DatabaseIntegrityFailed,
            QStringLiteral("The project database metadata is not valid."),
            QStringLiteral("The singleton project metadata row failed invariant validation or its "
                           "schema_version does not match PRAGMA user_version."),
            impl_->connection->path(),
            QStringLiteral("Restore a known-good project backup.")));
    }
    return domain::Result<domain::ProjectInfo>::success(std::move(project));
}

domain::Result<QVector<MigrationRecord>> SqliteProjectDatabase::migrationLedger() const
{
    auto prepared =
        impl_->connection->prepare("SELECT sequence, migration_id, checksum, application_version, "
                                   "started_at_utc, completed_at_utc, result "
                                   "FROM schema_migrations ORDER BY sequence;",
                                   QStringLiteral("read schema migration ledger"),
                                   domain::ErrorCode::DatabaseMigrationFailed);
    if (!prepared)
    {
        return domain::Result<QVector<MigrationRecord>>::failure(std::move(prepared).error());
    }
    Statement statement = std::move(prepared).value();
    QVector<MigrationRecord> records;
    for (;;)
    {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE)
        {
            break;
        }
        if (step != SQLITE_ROW)
        {
            return domain::Result<QVector<MigrationRecord>>::failure(
                sqliteFailure(impl_->connection->get(),
                              step,
                              impl_->connection->path(),
                              QStringLiteral("iterate schema migration ledger"),
                              domain::ErrorCode::DatabaseMigrationFailed));
        }
        const auto textColumn = [&statement](const int index)
        {
            const auto* value = sqlite3_column_text(statement.get(), index);
            return value != nullptr ? QString::fromUtf8(reinterpret_cast<const char*>(value))
                                    : QString{};
        };
        QDateTime startedAt = QDateTime::fromString(textColumn(4), Qt::ISODateWithMs);
        QDateTime completedAt = QDateTime::fromString(textColumn(5), Qt::ISODateWithMs);
        records.append({
            .sequence = sqlite3_column_int(statement.get(), 0),
            .migrationId = textColumn(1),
            .checksum = textColumn(2),
            .applicationVersion = textColumn(3),
            .startedAtUtc = startedAt.toUTC(),
            .completedAtUtc = completedAt.toUTC(),
            .result = textColumn(6),
        });
    }
    return domain::Result<QVector<MigrationRecord>>::success(std::move(records));
}

SqliteRuntimeInfo SqliteProjectDatabase::runtimeInfo() const
{
    return collectRuntimeInfo();
}

QString SqliteProjectDatabase::initialMigrationId()
{
    return QString::fromLatin1(kInitialMigrationId);
}

QString SqliteProjectDatabase::initialMigrationChecksum()
{
    return checksumForInitialMigration();
}

} // namespace corax::storage_sqlite
