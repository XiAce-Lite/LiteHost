#pragma once

#include <JuceHeader.h>

/** Stereo-linked noise gate with ~4 dB hysteresis. Audio thread except prepare(). */
class NoiseGate
{
public:
    void prepare (double sampleRate) noexcept;
    /** startClosed: fade in from silence (device open). Otherwise pass audio immediately. */
    void reset (bool startClosed) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int numSamples, float thresholdDb) noexcept;

private:
    float env = 0.0f;
    float gain = 1.0f;
    bool open = true;
    float attack = 1.0f;
    float release = 1.0f;
    float envRelease = 1.0f;
};
