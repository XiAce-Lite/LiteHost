#pragma once

#include "TrackProcessor.h"
#include <atomic>
#include <memory>
#include <vector>

/** Real-time helper: process independent tracks in parallel, then mix on the audio thread. */
class ParallelTrackExecutor
{
public:
    struct Job
    {
        TrackProcessor* track = nullptr;
        const juce::MidiBuffer* midi = nullptr;
    };

    ParallelTrackExecutor();
    ~ParallelTrackExecutor();

    ParallelTrackExecutor (const ParallelTrackExecutor&) = delete;
    ParallelTrackExecutor& operator= (const ParallelTrackExecutor&) = delete;

    void ensureStarted();
    void shutdown();

    /** Process each job's processInputs, then mixTo master (serial). Audio-thread only.
        When allowParallel is false (or too few jobs), runs entirely on the calling thread. */
    void processAndMix (Job* jobs,
                        int numJobs,
                        const float* const* inputs,
                        int numInputChannels,
                        int numSamples,
                        juce::AudioBuffer<float>& master,
                        bool allowParallel) noexcept;

    int getWorkerCount() const noexcept { return (int) workers.size(); }

private:
    class Worker;
    friend class Worker;

    void workerLoop() noexcept;
    void runJobsOnThisThread() noexcept;
    void processSerial (Job* jobList,
                        int numJobs,
                        const float* const* inputs,
                        int numInputChannels,
                        int numSamples,
                        juce::AudioBuffer<float>& master) noexcept;

    std::vector<std::unique_ptr<Worker>> workers;

    Job* activeJobs = nullptr;
    int activeJobCount = 0;
    const float* const* inputChannels = nullptr;
    int numInputChannels = 0;
    int numSamples = 0;

    std::atomic<int> nextJob { 0 };
    std::atomic<int> completed { 0 };
    std::atomic<bool> exitFlag { false };

    juce::WaitableEvent doneEvent;
    bool started = false;
};
