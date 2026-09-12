#pragma once

#include "HostPlayHead.h"
#include "ParallelTrackExecutor.h"
#include "TrackProcessor.h"
#include <atomic>
#include <vector>

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    static constexpr int maxBlockSize = 8192;
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

    TrackProcessor* addTrack (juce::String name);
    void removeTrack (const juce::Uuid& id);
    /** Move track at fromIndex to destIndex (after the source has been removed). Caller holds the callback lock. */
    bool moveTrack (int fromIndex, int destIndex);
    void clearTracksAndMaster();
    TrackProcessor* findTrack (const juce::Uuid& id) const;
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

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    /** Smoothed average DSP load (0–1). */
    float getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }
    /** Slow-decay peak load for a readable meter (0–1). */
    float getCpuPeakLoad() const noexcept { return cpuPeakLoad.load (std::memory_order_relaxed); }
    int getTrackCount() const noexcept { return trackCount.load (std::memory_order_relaxed); }

private:
    struct PendingMidi
    {
        juce::String deviceId;
        juce::MidiMessage message;
    };

    void prepareGraph();
    void updateBuiltInParameters() noexcept;
    void applyPeakLimiter (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    void resetPeakLimiter() noexcept;
    void applyGate (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    void copyToOutputs (float* const* outputs, int numOutputChannels, int numSamples) noexcept;
    void applyOutputFadeIn (int numSamples) noexcept;
    void drainPendingMidi (int numSamples) noexcept;
    const juce::MidiBuffer* findDeviceMidi (const juce::String& deviceId) const noexcept;
    void applyPanicIfNeeded (int numSamples) noexcept;
    static void buildPanicMidi (juce::MidiBuffer& dest);
    void processActiveTracks (const float* const* inputChannelData,
                              int numInputChannels,
                              int numSamples,
                              bool anySolo) noexcept;

    HostPlayHead playHead;
    std::vector<std::unique_ptr<TrackProcessor>> tracks_;
    PluginChain masterPlugins_;
    ParallelTrackExecutor trackExecutor;
    juce::AudioBuffer<float> masterBus;
    juce::AudioBuffer<float> limiterDelay;
    juce::HeapBlock<float> limiterPeakRing;
    juce::dsp::Reverb reverb;
    juce::dsp::ProcessSpec spec {};
    juce::CriticalSection callbackLock;
    juce::CriticalSection midiLock;
    juce::Array<PendingMidi> pendingMidi;
    juce::Array<PendingMidi> midiDrainBatch;
    juce::MidiBuffer midiAllScratch;
    static constexpr int maxMidiDeviceBuckets = 16;
    juce::String midiDeviceBucketIds[maxMidiDeviceBuckets];
    juce::MidiBuffer midiDeviceBuckets[maxMidiDeviceBuckets];
    int numMidiDeviceBuckets = 0;
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
    float gateEnv = 0.0f;
    float gateGain = 1.0f;
    bool gateOpen = true;
    int limiterLookAhead = 0;
    int limiterWrite = 0;
    float limiterGain = 1.0f;
    float limiterAttack = 1.0f;
    float limiterRelease = 1.0f;
    float limiterWindowPeak = 0.0f;
    static constexpr int maxParallelTrackJobs = 64;
};
