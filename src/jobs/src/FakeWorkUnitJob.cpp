#include <corax/jobs/FakeWorkUnitJob.h>

#include <QCoreApplication>

#include <algorithm>
#include <utility>

namespace corax::jobs
{

FakeWorkUnitJob::FakeWorkUnitJob(JobDescriptor descriptor,
                                 const int workUnits,
                                 const std::chrono::milliseconds unitDuration)
    : descriptor_(std::move(descriptor)), workUnits_(std::max(0, workUnits)),
      unitDuration_(std::max(std::chrono::milliseconds::zero(), unitDuration))
{
}

const JobDescriptor& FakeWorkUnitJob::descriptor() const noexcept
{
    return descriptor_;
}

JobOutcome FakeWorkUnitJob::run(JobContext& context)
{
    const auto phaseLabel =
        QCoreApplication::translate("FakeWorkUnitJob", "Checking background work");

    context.reportProgress({
        .phaseId = QStringLiteral("fake_work_units"),
        .phaseLabel = phaseLabel,
        .completedUnits = 0,
        .totalUnits = workUnits_,
        .currentItem = {},
        .issueCount = 0,
    });

    for (int completed = 0; completed < workUnits_; ++completed)
    {
        if (context.isCancellationRequested() ||
            (unitDuration_.count() > 0 &&
             context.cancellationToken().waitForCancellationFor(unitDuration_)))
        {
            return JobOutcome::canceled();
        }

        context.reportProgress({
            .phaseId = QStringLiteral("fake_work_units"),
            .phaseLabel = phaseLabel,
            .completedUnits = completed + 1,
            .totalUnits = workUnits_,
            .currentItem = {},
            .issueCount = 0,
        });
    }

    return context.isCancellationRequested() ? JobOutcome::canceled() : JobOutcome::succeeded();
}

} // namespace corax::jobs
