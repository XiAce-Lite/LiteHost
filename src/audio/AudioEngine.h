#pragma once

#include "DetachedObjects.h"
#include "HostPlayHead.h"
#include "MidiInputRouter.h"
#include "NoiseGate.h"
#include "ParallelTrackExecutor.h"
#include "PeakLimiter.h"
#include "TrackProcessor.h"
#include <atomic>
#include <vector>

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    static constexpr int maxBlockSize = 8192;
    static constexpr int maxReturns = TrackProcessor::maxSends;
    static constexpr const char* midiAllDevicesId = "*";

    AudioEngine();

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    /** Queue All Notes Off / All Sound Off for the next audio callback (all tracks + master). */
    void requestPanic() noexcept;

    /** Run `edit (DetachedObjects&)` under the callback lock. Tracks / plugins it detaches are
        destroyed when the returned object dies — always after the lock is released. Keep the
        return value alive when UI still references the detached objects (e.g. until strips rebuild). */
    template <typename Edit>
    DetachedObjects editGraph (Edit&& edit)
    {
        DetachedObjects detached;
        {
            const juce::ScopedLock sl (callbackLock);
            edit (detached);
        }
        return detached;
    }

    TrackProcessor* addTrack (juce::String name);
    /** Null when maxReturns already exist. Caller holds the callback lock. */
    TrackProcessor* addReturn (juce::String name);
    /** Removes a track or return bus. Removing a return shifts later sends down. Caller holds the callback lock. */
    void removeTrack (const juce::Uuid& id, DetachedObjects& detached);
    /** Move track at fromIndex to destIndex (after the source has been removed). Caller holds the callback lock. */
    bool moveTrack (int fromIndex, int destIndex);
    /** Remove every track, return and master plugin and reset master settings. Caller holds the callback lock. */
    void clearTracksAndMaster (DetachedObjects& detached);

    /** Message thread. Sets the track's MIDI source ("" = none, "*" = all devices). */
    void setTrackMidiDevice (TrackProcessor& track, const juce::String& deviceId);
    /** Searches tracks and return buses. */
    TrackProcessor* findTrack (const juce::Uuid& id) const;
    const std::vector<std::unique_ptr<TrackProcessor>>& returns() const noexcept { return returns_; }
    int indexOfReturn (const TrackProcessor& bus) const noexcept;
    bool hasSoloOverride() const noexcept;
    void setTrackSolo (TrackProcessor& track, bool shouldSolo);
    const std::vector<std::unique_ptr<TrackProcessor>>& tracks() const noexcept { return tracks_; }
    const juce::CriticalSection& getCallbackLock() const noexcept { return callbackLock; }
    HostPlayHead& getPlayHead() noexcept { return playHead; }

    PluginChain& masterPlugins() noexcept { return masterPlugins_; }
    const PluginChain& masterPlugins() const noexcept { return masterPlugins_; }

    std::atomic<bool> reverbEnabled { false };
    std::atomic<bool> limiterEnabled { false };
    std::atomic<bool> gateEnabled { false };
    std::atomic<float> reverbRoom { 0.42f };
    std::atomic<float> reverbDamping { 0.4f };
    std::atomic<float> reverbWet { 0.18f };
    std::atomic<float> reverbWidth { 1.0f };
    std::atomic<float> limiterThresholdDb { -0.3f };
    static constexpr float limiterCeilingMinDb = -4.0f;
    static constexpr float limiterCeilingMaxDb = 0.0f;
    /** Noise gate open threshold. Kept modest so Max cannot mute a normal performance. */
    std::atomic<float> gateThresholdDb { -52.0f };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<float> masterPeak { 0.0f };
    /** Cakewalk Exclusive Solo: next solo click unsilos other tracks. Override is kept. */
    std::atomic<bool> exclusiveSoloMode { false };
    /** When true, active tracks may process on helper threads (see ParallelTrackExecutor). */
    std::atomic<bool> parallelTracksEnabled { true };

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    /** Smoothed average DSP load (0–1). */
    float getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }
    /** Slow-decay peak load for a readable meter (0–1). */
    float getCpuPeakLoad() const noexcept { return cpuPeakLoad.load (std::memory_order_relaxed); }
    int getTrackCount() const noexcept { return trackCount.load (std::memory_order_relaxed); }

private:
    void prepareGraph();
    void updateBuiltInParameters() noexcept;
    void copyToOutputs (float* const* outputs, int numOutputChannels, int numSamples) noexcept;
    void applyOutputFadeIn (int numSamples) noexcept;
    void applyPanicIfNeeded (int numSamples) noexcept;
    static void buildPanicMidi (juce::MidiBuffer& dest);
    void processActiveTracks (const float* const* inputChannelData,
                              int numInputChannels,
                              int numSamples,
                              bool anySolo) noexcept;

    HostPlayHead playHead;
    void processReturns (int numSamples) noexcept;

    std::vector<std::unique_ptr<TrackProcessor>> tracks_;
    std::vector<std::unique_ptr<TrackProcessor>> returns_;
    juce::AudioBuffer<float> returnInputs[maxReturns];
    PluginChain masterPlugins_;
    ParallelTrackExecutor trackExecutor;
    MidiInputRouter midiRouter;
    NoiseGate gate;
    PeakLimiter limiter;
    juce::AudioBuffer<float> masterBus;
    juce::dsp::Reverb reverb;
    juce::dsp::ProcessSpec spec {};
    juce::CriticalSection callbackLock;
    juce::MidiBuffer panicMidi;
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<float> cpuPeakLoad { 0.0f };
    std::atomic<int> trackCount { 0 };
    std::atomic<bool> panicPending { false };
    float appliedReverbRoom = -1.0f;
    float appliedReverbDamping = -1.0f;
    float appliedReverbWet = -1.0f;
    float appliedReverbWidth = -1.0f;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool running = false;
    int outputFadeSamplesRemaining = 0;
    static constexpr int maxParallelTrackJobs = 256;
};
