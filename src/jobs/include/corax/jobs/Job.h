#pragma once

#include <corax/jobs/CancellationToken.h>
#include <corax/jobs/JobTypes.h>

#include <functional>
#include <memory>

namespace corax::jobs
{

class JobContext final
{
public:
    using ProgressReporter = std::function<void(JobProgress)>;

    [[nodiscard]] const CancellationToken& cancellationToken() const noexcept;
    [[nodiscard]] bool isCancellationRequested() const noexcept;
    void reportProgress(JobProgress progress) const;

private:
    JobContext(CancellationToken cancellationToken, ProgressReporter progressReporter);

    CancellationToken cancellationToken_;
    ProgressReporter progressReporter_;

    friend class JobScheduler;
};

class IJob
{
public:
    virtual ~IJob() = default;

    // Job contract:
    //
    // - run() must bound every external, filesystem, process, and database wait.
    // - Cancellable jobs must observe the cancellation token at safe points.
    // - Noncancellable jobs are reserved for short, recoverable atomic work on the
    //   serialized DatabaseWrite lane.
    //
    // The scheduler never detaches workers. If a job violates this contract during
    // process shutdown, the scheduler uses a fail-stop after its hard deadline so
    // that no worker can outlive scheduler state or captured dependencies.
    [[nodiscard]] virtual const JobDescriptor& descriptor() const noexcept = 0;
    [[nodiscard]] virtual JobOutcome run(JobContext& context) = 0;
};

class FunctionalJob final : public IJob
{
public:
    using Work = std::function<JobOutcome(JobContext&)>;

    FunctionalJob(JobDescriptor descriptor, Work work);

    [[nodiscard]] const JobDescriptor& descriptor() const noexcept override;
    [[nodiscard]] JobOutcome run(JobContext& context) override;

private:
    JobDescriptor descriptor_;
    Work work_;
};

} // namespace corax::jobs
