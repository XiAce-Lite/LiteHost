#include "PeakLimiter.h"
#include <cmath>

void PeakLimiter::WindowMax::prepare (int windowSamples)
{
    window = juce::jmax (1, windowSamples);
    // Holds at most `window` live entries plus the one pushed before the oldest expires.
    ringSize = window + 1;
    values.allocate ((size_t) ringSize, true);
    times.allocate ((size_t) ringSize, true);
    reset();
}

void PeakLimiter::WindowMax::reset() noexcept
{
    head = 0;
    count = 0;
    now = 0;
}

float PeakLimiter::WindowMax::push (float value) noexcept
{
    const auto t = now++;

    // Values strictly decrease from head to tail, so the head is the window max.
    while (count > 0)
    {
        int tail = head + count - 1;
        if (tail >= ringSize)
            tail -= ringSize;
        if (values[tail] > value)
            break;
        --count;
    }

    int slot = head + count;
    if (slot >= ringSize)
        slot -= ringSize;
    values[slot] = value;
    times[slot] = t;
    ++count;

    if (t - times[head] >= (juce::uint32) window)
    {
        if (++head == ringSize)
            head = 0;
        --count;
    }

    return values[head];
}

void PeakLimiter::prepare (double sampleRate)
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;

    // ~2 ms look-ahead keeps live latency small while avoiding instant-clip harshness.
    lookAhead = juce::jlimit (1, 512, (int) std::lround (sr * 0.002));
    delay.setSize (2, lookAhead, false, true, true);
    windowMax.prepare (lookAhead);

    attack = 1.0f - std::exp (-1.0f / (0.0005f * (float) sr));  // ~0.5 ms
    release = 1.0f - std::exp (-1.0f / (0.080f * (float) sr));  // ~80 ms
    reset();
}

void PeakLimiter::reset() noexcept
{
    gain = 1.0f;
    writePos = 0;
    windowMax.reset();
    if (delay.getNumSamples() > 0)
        delay.clear();
}

void PeakLimiter::process (juce::AudioBuffer<float>& buffer, int numSamples, float ceilingDb) noexcept
{
    if (numSamples <= 0 || lookAhead <= 0 || delay.getNumSamples() < lookAhead)
        return;

    const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    if (ceiling <= 0.0f)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    auto* delayL = delay.getWritePointer (0);
    auto* delayR = delay.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right[i];

        const float outL = delayL[writePos];
        const float outR = delayR[writePos];

        delayL[writePos] = inL;
        delayR[writePos] = inR;

        const float windowPeak = windowMax.push (juce::jmax (std::abs (inL), std::abs (inR)));
        const float target = windowPeak > ceiling ? (ceiling / windowPeak) : 1.0f;
        const float coeff = target < gain ? attack : release;
        gain += (target - gain) * coeff;

        left[i] = outL * gain;
        if (right != left)
            right[i] = outR * gain;

        if (++writePos == lookAhead)
            writePos = 0;
    }
}
