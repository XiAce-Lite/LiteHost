#pragma once

#include "TrackProcessor.h"
#include <atomic>
#include <cstdint>
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

    /** Process each job's processInputs, then mixTo master and add sends to returnInputs (serial).
        Audio-thread only. When allowParallel is false (or too few jobs), runs entirely on the calling thread. */
    void processAndMix (Job* jobs,
                        int numJobs,
                        const float* const* inputs,
                        int numInputChannels,
                        int numSamples,
                        juce::AudioBuffer<float>& master,
                        juce::AudioBuffer<float>* returnInputs,
                        int numReturns,
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
                        juce::AudioBuffer<float>& master,
                        juce::AudioBuffer<float>* returnInputs,
                        int numReturns) noexcept;

    static constexpr std::uint32_t closedIndex = 0xffffffffu;
    static constexpr std::uint64_t packCursor (std::uint32_t gen, std::uint32_t index) noexcept
    {
        return ((std::uint64_t) gen << 32) | index;
    }
    static constexpr std::uint32_t cursorIndex (std::uint64_t packed) noexcept { return (std::uint32_t) packed; }

    std::vector<std::unique_ptr<Worker>> workers;

    /** (generation << 32) | next job index. A claim is a CAS on this, so it fails once the batch changes. */
    std::atomic<std::uint64_t> cursor { packCursor (0, closedIndex) };
    std::uint32_t generation = 0; // audio thread only

    std::atomic<Job*> batchJobs { nullptr };
    std::atomic<int> batchCount { 0 };
    std::atomic<const float* const*> batchInputs { nullptr };
    std::atomic<int> batchNumInputs { 0 };
    std::atomic<int> batchNumSamples { 0 };

    std::atomic<int> completed { 0 };
    std::atomic<bool> exitFlag { false };

    juce::WaitableEvent doneEvent;
    bool started = false;
};
