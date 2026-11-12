#include "NoiseGate.h"
#include <cmath>

void NoiseGate::prepare (double sampleRate) noexcept
{
    const float sr = sampleRate > 0.0 ? (float) sampleRate : 48000.0f;
    attack = 1.0f - std::exp (-1.0f / (0.003f * sr));
    release = 1.0f - std::exp (-1.0f / (0.080f * sr));
    envRelease = 1.0f - std::exp (-1.0f / (0.020f * sr));
}

void NoiseGate::reset (bool startClosed) noexcept
{
    env = 0.0f;
    gain = startClosed ? 0.0f : 1.0f;
    open = ! startClosed;
}

void NoiseGate::process (juce::AudioBuffer<float>& buffer, int numSamples, float thresholdDb) noexcept
{
    if (numSamples <= 0)
        return;

    const float openLin = juce::Decibels::decibelsToGain (thresholdDb);
    const float closeLin = openLin * 0.63f; // ~4 dB hysteresis

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    for (int i = 0; i < numSamples; ++i)
    {
        const float peak = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        if (peak > env)
            env = peak;
        else
            env += (peak - env) * envRelease;

        if (open)
        {
            if (env < closeLin)
                open = false;
        }
        else if (env >= openLin)
        {
            open = true;
        }

        const float target = open ? 1.0f : 0.0f;
        gain += (target - gain) * (open ? attack : release);

        if (gain <= 1.0e-4f)
        {
            left[i] = 0.0f;
            if (right != left)
                right[i] = 0.0f;
        }
        else if (gain < 0.999f)
        {
            left[i] *= gain;
            if (right != left)
                right[i] *= gain;
        }
    }
}
