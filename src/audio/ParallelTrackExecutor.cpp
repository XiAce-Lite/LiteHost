#include "ParallelTrackExecutor.h"

class ParallelTrackExecutor::Worker final : public juce::Thread
{
public:
    Worker (ParallelTrackExecutor& ownerToUse, int index)
        : juce::Thread ("LHTrack" + juce::String (index)),
          owner (ownerToUse)
    {
        startThread (juce::Thread::Priority::highest);
    }

    ~Worker() override
    {
        kickEvent.signal();
        stopThread (2000);
    }

    void kick() noexcept { kickEvent.signal(); }

    void run() override
    {
        juce::ScopedNoDenormals noDenormals;

        while (! threadShouldExit() && ! owner.exitFlag.load (std::memory_order_acquire))
        {
            if (! kickEvent.wait (-1))
                continue;

            if (threadShouldExit() || owner.exitFlag.load (std::memory_order_acquire))
                break;

            owner.workerLoop();
        }
    }

private:
    ParallelTrackExecutor& owner;
    juce::WaitableEvent kickEvent; // auto-reset
};

ParallelTrackExecutor::ParallelTrackExecutor() = default;

ParallelTrackExecutor::~ParallelTrackExecutor()
{
    shutdown();
}

void ParallelTrackExecutor::ensureStarted()
{
    if (started)
        return;

    const int cpus = juce::jmax (1, juce::SystemStats::getNumCpus());
    // Audio thread also steals work; spawn (cpus - 1) helpers, cap to keep scheduling light.
    const int helperCount = juce::jlimit (0, 8, cpus - 1);

    exitFlag.store (false, std::memory_order_release);
    workers.reserve ((size_t) helperCount);
    for (int i = 0; i < helperCount; ++i)
        workers.push_back (std::make_unique<Worker> (*this, i));

    started = true;
}

void ParallelTrackExecutor::shutdown()
{
    if (! started)
        return;

    exitFlag.store (true, std::memory_order_release);
    for (auto& worker : workers)
        if (worker != nullptr)
            worker->kick();

    workers.clear();
    doneEvent.reset();
    started = false;
}

void ParallelTrackExecutor::workerLoop() noexcept
{
    runJobsOnThisThread();
}

void ParallelTrackExecutor::runJobsOnThisThread() noexcept
{
    std::atomic_thread_fence (std::memory_order_acquire);

    const int total = activeJobCount;
    auto* jobList = activeJobs;
    if (total <= 0 || jobList == nullptr)
        return;

    for (;;)
    {
        const int index = nextJob.fetch_add (1, std::memory_order_relaxed);
        if (index >= total)
            break;

        auto& job = jobList[index];
        if (job.track != nullptr && job.midi != nullptr)
            job.track->processInputs (inputChannels, numInputChannels, numSamples, *job.midi);

        if (completed.fetch_add (1, std::memory_order_acq_rel) + 1 >= total)
            doneEvent.signal();
    }
}

void ParallelTrackExecutor::processAndMix (Job* jobList,
                                           int numJobs,
                                           const float* const* inputs,
                                           int numInputChannelsIn,
                                           int numSamplesIn,
                                           juce::AudioBuffer<float>& master) noexcept
{
    if (jobList == nullptr || numJobs <= 0 || numSamplesIn <= 0)
        return;

    if (numJobs == 1 || ! started || workers.empty())
    {
        for (int i = 0; i < numJobs; ++i)
        {
            auto& job = jobList[i];
            if (job.track == nullptr || job.midi == nullptr)
                continue;
            job.track->processInputs (inputs, numInputChannelsIn, numSamplesIn, *job.midi);
            job.track->mixTo (master, numSamplesIn);
        }
        return;
    }

    activeJobs = jobList;
    activeJobCount = numJobs;
    inputChannels = inputs;
    numInputChannels = numInputChannelsIn;
    numSamples = numSamplesIn;
    nextJob.store (0, std::memory_order_relaxed);
    completed.store (0, std::memory_order_relaxed);
    doneEvent.reset();
    std::atomic_thread_fence (std::memory_order_release);

    for (auto& worker : workers)
        worker->kick();

    runJobsOnThisThread();

    while (completed.load (std::memory_order_acquire) < numJobs)
        doneEvent.wait (-1);

    for (int i = 0; i < numJobs; ++i)
        if (jobList[i].track != nullptr)
            jobList[i].track->mixTo (master, numSamplesIn);
}
