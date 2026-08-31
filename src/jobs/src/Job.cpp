#include <corax/jobs/Job.h>

#include <utility>

namespace corax::jobs
{

JobContext::JobContext(CancellationToken cancellationToken, ProgressReporter progressReporter)
    : cancellationToken_(std::move(cancellationToken)),
      progressReporter_(std::move(progressReporter))
{
}

const CancellationToken& JobContext::cancellationToken() const noexcept
{
    return cancellationToken_;
}

bool JobContext::isCancellationRequested() const noexcept
{
    return cancellationToken_.isCancellationRequested();
}

void JobContext::reportProgress(JobProgress progress) const
{
    if (progressReporter_)
    {
        progressReporter_(std::move(progress));
    }
}

FunctionalJob::FunctionalJob(JobDescriptor descriptor, Work work)
    : descriptor_(std::move(descriptor)), work_(std::move(work))
{
}

const JobDescriptor& FunctionalJob::descriptor() const noexcept
{
    return descriptor_;
}

JobOutcome FunctionalJob::run(JobContext& context)
{
    if (!work_)
    {
        return JobOutcome::failed({
            .code = QStringLiteral("jobs.missing_work"),
            .userMessage = QStringLiteral("The background task could not start."),
            .technicalContext = QStringLiteral("The job has no work function."),
            .remediation = QStringLiteral("Try the task again."),
            .retryable = true,
        });
    }
    return work_(context);
}

} // namespace corax::jobs
