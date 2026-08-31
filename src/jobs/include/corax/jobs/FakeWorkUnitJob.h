#pragma once

#include <corax/jobs/Job.h>

#include <chrono>

namespace corax::jobs
{

// Milestone 0 architecture probe. Replace this job when the first real product
// job arrives. It deliberately performs no project or asset work.
class FakeWorkUnitJob final : public IJob
{
public:
    FakeWorkUnitJob(JobDescriptor descriptor,
                    int workUnits,
                    std::chrono::milliseconds unitDuration);

    [[nodiscard]] const JobDescriptor& descriptor() const noexcept override;
    [[nodiscard]] JobOutcome run(JobContext& context) override;

private:
    JobDescriptor descriptor_;
    int workUnits_{0};
    std::chrono::milliseconds unitDuration_{0};
};

} // namespace corax::jobs
