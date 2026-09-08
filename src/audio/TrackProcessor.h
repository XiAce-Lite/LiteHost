#pragma once

#include "PluginChain.h"
#include <atomic>
#include <JuceHeader.h>

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
