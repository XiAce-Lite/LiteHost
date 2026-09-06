#pragma once

#include "PluginChain.h"
#include <atomic>
#include <vector>

class HostPlayHead final : public juce::AudioPlayHead
{
public:
    void setSampleRate (double sr) noexcept { sampleRate.store (sr, std::memory_order_relaxed); }
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay, std::memory_order_relaxed); }
    void advance (int numSamples) noexcept
    {
        samplePosition.fetch_add ((juce::int64) numSamples, std::memory_order_relaxed);
    }
    void resetPosition() noexcept { samplePosition.store (0, std::memory_order_relaxed); }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        const auto pos = samplePosition.load (std::memory_order_relaxed);
        const auto sr = sampleRate.load (std::memory_order_relaxed);

        info.setTimeInSamples (pos);
        if (sr > 0.0)
            info.setTimeInSeconds ((double) pos / sr);
        info.setBpm (juce::Optional<double> (120.0));
        info.setIsPlaying (playing.load (std::memory_order_relaxed));
        info.setIsRecording (false);
        info.setIsLooping (false);
        info.setTimeSignature (juce::Optional<juce::AudioPlayHead::TimeSignature> (
            juce::AudioPlayHead::TimeSignature { 4, 4 }));
        return info;
    }

private:
    std::atomic<juce::int64> samplePosition { 0 };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> playing { true };
};

class TrackProcessor
{
public:
    explicit TrackProcessor (juce::String nameToUse);

    juce::String name;
    const juce::Uuid id;

    std::atomic<int> inputStart { 0 };
    std::atomic<int> inputCount { 2 };
    /** Non-empty: take MIDI from this device identifier, or "*" for all devices. */
    juce::String midiDeviceId;
    std::atomic<float> trim { 1.0f };
    std::atomic<float> gain { 1.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
    /** Shift+solo: stay audible when other tracks are soloed. Mute still wins. */
    std::atomic<bool> soloOverride { false };
    std::atomic<float> peak { 0.0f };

    PluginChain plugins;

    void prepare (double sampleRate, int samplesPerBlock, juce::AudioPlayHead* playHead);
    void release();
    void processInputs (const float* const* inputs,
                        int numInputChannels,
                        int numSamples,
                        const juce::MidiBuffer& midi) noexcept;
    void mixTo (juce::AudioBuffer<float>& master, int numSamples) const noexcept;

private:
    juce::AudioBuffer<float> work;
};

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
    void applyCeiling (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    void applyGate (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    void copyToOutputs (float* const* outputs, int numOutputChannels, int numSamples) noexcept;
    void drainPendingMidi (int numSamples) noexcept;
    const juce::MidiBuffer* findDeviceMidi (const juce::String& deviceId) const noexcept;

    HostPlayHead playHead;
    std::vector<std::unique_ptr<TrackProcessor>> tracks_;
    PluginChain masterPlugins_;
    juce::AudioBuffer<float> masterBus;
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
    float appliedReverbRoom = -1.0f;
    float appliedReverbDamping = -1.0f;
    float appliedReverbWet = -1.0f;
    float appliedReverbWidth = -1.0f;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool running = false;
    float gateEnv = 0.0f;
    float gateGain = 1.0f;
    bool gateOpen = true;
};
