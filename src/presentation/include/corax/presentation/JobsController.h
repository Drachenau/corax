#pragma once

#include <corax/presentation/JobListModel.h>

#include <QObject>
#include <QString>

namespace corax::presentation
{

class JobsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(corax::presentation::JobListModel* jobs READ jobs CONSTANT)
    Q_PROPERTY(bool acceptingWork READ acceptingWork NOTIFY acceptingWorkChanged)

public:
    explicit JobsController(jobs::JobScheduler& scheduler, QObject* parent = nullptr);

    [[nodiscard]] JobListModel* jobs() noexcept;
    [[nodiscard]] bool acceptingWork() const noexcept;

    Q_INVOKABLE QString startFakeJob();
    Q_INVOKABLE bool cancelJob(const QString& jobId);

signals:
    void acceptingWorkChanged();

private:
    jobs::JobScheduler& scheduler_;
    JobListModel model_;
};

} // namespace corax::presentation
