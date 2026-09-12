#include "ParallelTrackExecutor.h"

class ParallelTrackExecutor::Worker final : public juce::Thread
{
public:
    Worker (ParallelTrackExecutor& ownerToUse, int index)
        : juce::Thread ("LHTrack" + juce::String (index)),
          owner (ownerToUse)
    {
        // Keep helpers below the audio callback priority so they don't fight the I/O thread.
        // On Apple Silicon, highest-priority helpers tend to wake P-cores and inflate occupancy.
       #if JUCE_MAC
        startThread (juce::Thread::Priority::normal);
       #else
        startThread (juce::Thread::Priority::high);
       #endif
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
   #if JUCE_MAC
    // Prefer fewer helpers on hybrid Apple CPUs; audio thread still steals work.
    const int helperCount = juce::jlimit (0, 4, (cpus + 1) / 2);
   #else
    const int helperCount = juce::jlimit (0, 8, cpus - 1);
   #endif

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

void ParallelTrackExecutor::processSerial (Job* jobList,
                                           int numJobs,
                                           const float* const* inputs,
                                           int numInputChannelsIn,
                                           int numSamplesIn,
                                           juce::AudioBuffer<float>& master) noexcept
{
    for (int i = 0; i < numJobs; ++i)
    {
        auto& job = jobList[i];
        if (job.track == nullptr || job.midi == nullptr)
            continue;
        job.track->processInputs (inputs, numInputChannelsIn, numSamplesIn, *job.midi);
        job.track->mixTo (master, numSamplesIn);
    }
}

void ParallelTrackExecutor::processAndMix (Job* jobList,
                                           int numJobs,
                                           const float* const* inputs,
                                           int numInputChannelsIn,
                                           int numSamplesIn,
                                           juce::AudioBuffer<float>& master,
                                           bool allowParallel) noexcept
{
    if (jobList == nullptr || numJobs <= 0 || numSamplesIn <= 0)
        return;

    // Parallel wake/join costs more than it saves for a single heavy track (common live case).
    if (! allowParallel || numJobs < 2 || ! started || workers.empty())
    {
        processSerial (jobList, numJobs, inputs, numInputChannelsIn, numSamplesIn, master);
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

    // Wake only as many helpers as can usefully steal work (not the whole pool).
    const int helpersToKick = juce::jmin ((int) workers.size(), numJobs - 1);
    for (int i = 0; i < helpersToKick; ++i)
        workers[(size_t) i]->kick();

    runJobsOnThisThread();

    while (completed.load (std::memory_order_acquire) < numJobs)
        doneEvent.wait (-1);

    for (int i = 0; i < numJobs; ++i)
        if (jobList[i].track != nullptr)
            jobList[i].track->mixTo (master, numSamplesIn);
}
