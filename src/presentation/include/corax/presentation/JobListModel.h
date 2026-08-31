#pragma once

#include <corax/jobs/JobScheduler.h>

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QVector>

namespace corax::presentation
{

class JobListModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    Q_PROPERTY(bool hasActiveJobs READ hasActiveJobs NOTIFY activeCountChanged)
    Q_PROPERTY(QString activitySummary READ activitySummary NOTIFY activeCountChanged)

public:
    enum Role
    {
        JobIdRole = Qt::UserRole + 1,
        TitleRole,
        StateRole,
        StateLabelRole,
        PhaseLabelRole,
        CompletedUnitsRole,
        TotalUnitsRole,
        ProgressRole,
        IndeterminateRole,
        CanCancelRole,
        IssueCountRole,
        ErrorMessageRole,
        ErrorCodeRole,
        ErrorTechnicalContextRole,
        ErrorRemediationRole,
        ErrorRetryableRole,
        IssuesRole,
    };
    Q_ENUM(Role)

    explicit JobListModel(jobs::JobScheduler& scheduler, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int activeCount() const noexcept;
    [[nodiscard]] bool hasActiveJobs() const noexcept;
    [[nodiscard]] QString activitySummary() const;
    Q_INVOKABLE [[nodiscard]] QVariantMap detailsForJob(const QString& jobId) const;

signals:
    void countChanged();
    void activeCountChanged();

private:
    void addJob(const jobs::JobSnapshot& snapshot);
    void updateJob(const jobs::JobSnapshot& snapshot);
    void removeJob(const QUuid& jobId);
    void recalculateActiveCount();
    [[nodiscard]] qsizetype rowForId(const QUuid& jobId) const;
    [[nodiscard]] QString localizedState(jobs::JobState state) const;

    jobs::JobScheduler& scheduler_;
    QVector<jobs::JobSnapshot> jobs_;
    int activeCount_{0};
};

} // namespace corax::presentation
