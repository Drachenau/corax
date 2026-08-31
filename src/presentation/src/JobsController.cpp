#include <corax/presentation/JobsController.h>

#include <corax/jobs/FakeWorkUnitJob.h>

#include <QUuid>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

namespace corax::presentation
{

JobsController::JobsController(jobs::JobScheduler& scheduler, QObject* parent)
    : QObject(parent), scheduler_(scheduler), model_(scheduler, this)
{
    connect(&scheduler_,
            &jobs::JobScheduler::acceptingWorkChanged,
            this,
            [this] { emit acceptingWorkChanged(); });
}

JobListModel* JobsController::jobs() noexcept
{
    return &model_;
}

bool JobsController::acceptingWork() const noexcept
{
    return scheduler_.isAcceptingWork();
}

QString JobsController::startFakeJob()
{
    jobs::JobDescriptor descriptor{
        .id = QUuid::createUuid(),
        .type = QStringLiteral("milestone0.fake_work_units"),
        .owner = QStringLiteral("corax_jobs"),
        .payloadVersion = 1,
        .title = tr("Architecture demo job (temporary)"),
        .projectId = {},
        .priority = jobs::JobPriority::Foreground,
        .requiredLanes = {jobs::ResourceLane::Cpu},
        .cancellable = true,
    };

    const auto id =
        scheduler_.submit(std::make_unique<jobs::FakeWorkUnitJob>(std::move(descriptor), 40, 35ms));
    return id.isNull() ? QString{} : id.toString(QUuid::WithoutBraces);
}

bool JobsController::cancelJob(const QString& jobId)
{
    const auto id = QUuid::fromString(jobId);
    return !id.isNull() && scheduler_.requestCancellation(id);
}

} // namespace corax::presentation
