// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/domain/Result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>

#include <memory>

namespace corax::storage_sqlite
{

inline constexpr int kCurrentManifestVersion = 1;
inline constexpr auto kManifestFileName = "corax.project.json";
inline constexpr auto kDatabaseFileName = "project.sqlite3";

struct ProjectManifest final
{
    QUuid projectId;
    QString displayName;
    QString databaseFile{QString::fromLatin1(kDatabaseFileName)};
    QDateTime createdAtUtc;
    QString minimumCoraxVersion{QStringLiteral("0.1.0")};
    QJsonObject preservedFields;

    [[nodiscard]] bool isValid() const;
};

class IAtomicFileWriter
{
public:
    virtual ~IAtomicFileWriter() = default;
    [[nodiscard]] virtual domain::Result<void> writeAtomically(const QString& path,
                                                               const QByteArray& contents) = 0;
};

class QSaveFileWriter final : public IAtomicFileWriter
{
public:
    [[nodiscard]] domain::Result<void> writeAtomically(const QString& path,
                                                       const QByteArray& contents) override;
};

class ManifestStore final
{
public:
    explicit ManifestStore(std::shared_ptr<IAtomicFileWriter> writer = {});

    [[nodiscard]] domain::Result<ProjectManifest> read(const QString& path) const;
    [[nodiscard]] domain::Result<ProjectManifest> parse(const QByteArray& contents,
                                                        const QString& sourcePath = {}) const;
    [[nodiscard]] QByteArray serialize(const ProjectManifest& manifest) const;
    [[nodiscard]] domain::Result<void> write(const QString& path,
                                             const ProjectManifest& manifest) const;

private:
    std::shared_ptr<IAtomicFileWriter> writer_;
};

} // namespace corax::storage_sqlite
