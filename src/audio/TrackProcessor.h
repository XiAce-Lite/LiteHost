#pragma once

#include "MidiInputRouter.h"
#include "PluginChain.h"
#include <atomic>
#include <JuceHeader.h>

class TrackProcessor
{
public:
    static constexpr int maxSends = 4;

    /** isReturnBus: input comes from track sends instead of audio/MIDI inputs. */
    explicit TrackProcessor (juce::String nameToUse, bool isReturnBus = false);

    juce::String name;
    const juce::Uuid id;
    const bool isReturn;

    std::atomic<int> inputStart { 0 };
    std::atomic<int> inputCount { 2 };
    std::atomic<float> trim { 1.0f };
    std::atomic<float> gain { 1.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
    /** Shift+solo: stay audible when other tracks are soloed. Mute still wins. */
    std::atomic<bool> soloOverride { false };
    std::atomic<float> peak { 0.0f };
    /** Duration of the last processInputs (high-res ticks); used to schedule heavy tracks first. */
    std::atomic<juce::int64> lastProcessTicks { 0 };
    /** Linear send level per return bus (index = return position). */
    std::atomic<float> sendLevel[maxSends] {};
    /** true: tap after Trim, before the fader. false: follows the fader. */
    std::atomic<bool> sendPreFader[maxSends] {};

    PluginChain plugins;

    /** Message thread. Non-empty: MIDI device identifier, or "*" for all devices.
        Set via AudioEngine::setTrackMidiDevice. */
    const juce::String& getMidiDeviceId() const noexcept { return midiDeviceId; }
    int getMidiRoute() const noexcept { return midiRoute.load (std::memory_order_relaxed); }

    void prepare (double sampleRate, int samplesPerBlock, juce::AudioPlayHead* playHead);
    void release();
    void processInputs (const float* const* inputs,
                        int numInputChannels,
                        int numSamples,
                        const juce::MidiBuffer& midi) noexcept;
    /** Return buses: run the chain on the summed sends. */
    void processReturn (const juce::AudioBuffer<float>& sendSum, int numSamples) noexcept;
    void mixTo (juce::AudioBuffer<float>& master, int numSamples) const noexcept;
    void mixSendsTo (juce::AudioBuffer<float>* returnInputs, int numReturns, int numSamples) const noexcept;

private:
    friend class AudioEngine;

    void applyTrimAndMeter (int numSamples) noexcept;

    juce::String midiDeviceId;
    /** Audio-thread view of midiDeviceId (a MidiInputRouter route). */
    std::atomic<int> midiRoute { MidiInputRouter::routeNone };
    juce::AudioBuffer<float> work;
};
