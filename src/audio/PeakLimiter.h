#pragma once

#include <JuceHeader.h>

/** Look-ahead stereo peak limiter (~2 ms). prepare() allocates; reset()/process() are real-time safe. */
class PeakLimiter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& buffer, int numSamples, float ceilingDb) noexcept;

    int getLookAheadSamples() const noexcept { return lookAhead; }

private:
    /** Sliding-window max over the last `window` samples, O(1) amortised (monotonic deque). */
    class WindowMax
    {
    public:
        void prepare (int windowSamples);
        void reset() noexcept;
        float push (float value) noexcept;

    private:
        juce::HeapBlock<float> values;
        juce::HeapBlock<juce::uint32> times;
        int window = 0;
        int ringSize = 0;
        int head = 0;
        int count = 0;
        juce::uint32 now = 0;
    };

    juce::AudioBuffer<float> delay;
    WindowMax windowMax;
    int lookAhead = 0;
    int writePos = 0;
    float gain = 1.0f;
    float attack = 1.0f;
    float release = 1.0f;
};
