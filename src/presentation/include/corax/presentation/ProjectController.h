#pragma once

#include <corax/application/ProjectService.h>
#include <corax/jobs/JobScheduler.h>

#include <QObject>
#include <QString>
#include <QUrl>

namespace corax::presentation
{

class ProjectController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectId READ projectId NOTIFY projectChanged)
    Q_PROPERTY(QUrl projectLocation READ projectLocation NOTIFY projectChanged)
    Q_PROPERTY(qint64 projectRevision READ projectRevision NOTIFY projectChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString currentOperation READ currentOperation NOTIFY busyChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QString errorTechnicalContext READ errorTechnicalContext NOTIFY errorChanged)
    Q_PROPERTY(QString errorRemediation READ errorRemediation NOTIFY errorChanged)
    Q_PROPERTY(QString errorAffectedPath READ errorAffectedPath NOTIFY errorChanged)
    Q_PROPERTY(bool errorRetryable READ errorRetryable NOTIFY errorChanged)

public:
    ProjectController(application::ProjectService& projectService,
                      jobs::JobScheduler& scheduler,
                      QObject* parent = nullptr);

    [[nodiscard]] bool hasProject() const noexcept;
    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QString projectId() const;
    [[nodiscard]] QUrl projectLocation() const;
    [[nodiscard]] qint64 projectRevision() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString currentOperation() const;

    [[nodiscard]] bool hasError() const noexcept;
    [[nodiscard]] QString errorCode() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString errorTechnicalContext() const;
    [[nodiscard]] QString errorRemediation() const;
    [[nodiscard]] QString errorAffectedPath() const;
    [[nodiscard]] bool errorRetryable() const noexcept;

    Q_INVOKABLE void createProject(const QUrl& projectDirectory, const QString& displayName);
    Q_INVOKABLE void openProject(const QUrl& projectDirectory);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE void clearError();

signals:
    void projectChanged();
    void busyChanged();
    void errorChanged();
    void projectOpened();
    void projectClosed();

private:
    [[nodiscard]] QString localPathFromUrl(const QUrl& url);
    [[nodiscard]] bool beginOperation(QString operation);
    void endOperation();
    void applyProject(const domain::ProjectInfo& project);
    void clearProject();
    void applyError(const domain::AppError& error);
    void applySubmissionError();

    application::ProjectService& projectService_;
    jobs::JobScheduler& scheduler_;
    std::optional<domain::ProjectInfo> project_;
    QString currentOperation_;
    domain::AppError error_;
    bool busy_{false};
    bool hasError_{false};
};

} // namespace corax::presentation
