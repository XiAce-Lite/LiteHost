#pragma once

#include <atomic>
#include <JuceHeader.h>

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
