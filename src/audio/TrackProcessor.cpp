#include "TrackProcessor.h"
#include "AudioEngine.h"
#include <cmath>

TrackProcessor::TrackProcessor (juce::String nameToUse, bool isReturnBus)
    : name (std::move (nameToUse)),
      id (juce::Uuid()),
      isReturn (isReturnBus)
{
}

void TrackProcessor::prepare (double sampleRate, int samplesPerBlock, juce::AudioPlayHead* playHead)
{
    work.setSize (2, AudioEngine::maxBlockSize, false, true, true);
    plugins.prepare (sampleRate, samplesPerBlock, playHead);
}

void TrackProcessor::release()
{
    plugins.release();
}

void TrackProcessor::processInputs (const float* const* inputs,
                                    int numInputChannels,
                                    int numSamples,
                                    const juce::MidiBuffer& midi) noexcept
{
    const auto startTicks = juce::Time::getHighResolutionTicks();
    work.setSize (2, numSamples, false, false, true);
    work.clear();

    const bool useMidi = getMidiRoute() != MidiInputRouter::routeNone;
    const int start = inputStart.load (std::memory_order_relaxed);
    const int count = inputCount.load (std::memory_order_relaxed);

    if (! useMidi && start >= 0 && inputs != nullptr)
    {
        auto* left = work.getWritePointer (0);
        auto* right = work.getWritePointer (1);

        if (count <= 1)
        {
            if (start < numInputChannels && inputs[start] != nullptr)
            {
                juce::FloatVectorOperations::copy (left, inputs[start], numSamples);
                juce::FloatVectorOperations::copy (right, inputs[start], numSamples);
            }
        }
        else
        {
            if (start < numInputChannels && inputs[start] != nullptr)
                juce::FloatVectorOperations::copy (left, inputs[start], numSamples);

            const int rightIndex = start + 1;
            if (rightIndex < numInputChannels && inputs[rightIndex] != nullptr)
                juce::FloatVectorOperations::copy (right, inputs[rightIndex], numSamples);
            else
                juce::FloatVectorOperations::copy (right, left, numSamples);
        }
    }

    plugins.process (work, midi);
    applyTrimAndMeter (numSamples);

    lastProcessTicks.store (juce::Time::getHighResolutionTicks() - startTicks, std::memory_order_relaxed);
}

void TrackProcessor::processReturn (const juce::AudioBuffer<float>& sendSum, int numSamples) noexcept
{
    work.setSize (2, numSamples, false, false, true);
    for (int channel = 0; channel < 2; ++channel)
        work.copyFrom (channel, 0, sendSum, channel, 0, numSamples);

    static const juce::MidiBuffer emptyMidi;
    plugins.process (work, emptyMidi);
    applyTrimAndMeter (numSamples);
}

void TrackProcessor::applyTrimAndMeter (int numSamples) noexcept
{
    const float trimGain = trim.load (std::memory_order_relaxed);
    if (! juce::approximatelyEqual (trimGain, 1.0f))
        work.applyGain (0, numSamples, trimGain);

    const float magnitude = work.getMagnitude (0, numSamples);
    const float previous = peak.load (std::memory_order_relaxed);
    peak.store (juce::jmax (previous * 0.6f, magnitude), std::memory_order_relaxed);
}

void TrackProcessor::mixSendsTo (juce::AudioBuffer<float>* returnInputs, int numReturns, int numSamples) const noexcept
{
    if (returnInputs == nullptr)
        return;

    const float fader = gain.load (std::memory_order_relaxed);
    for (int i = 0; i < juce::jmin (numReturns, maxSends); ++i)
    {
        float level = sendLevel[i].load (std::memory_order_relaxed);
        if (level <= 0.0f)
            continue;

        if (! sendPreFader[i].load (std::memory_order_relaxed))
            level *= fader;

        auto& dest = returnInputs[i];
        juce::FloatVectorOperations::addWithMultiply (dest.getWritePointer (0), work.getReadPointer (0), level, numSamples);
        juce::FloatVectorOperations::addWithMultiply (dest.getWritePointer (1), work.getReadPointer (1), level, numSamples);
    }
}

void TrackProcessor::mixTo (juce::AudioBuffer<float>& master, int numSamples) const noexcept
{
    const float g = gain.load (std::memory_order_relaxed);
    const float p = juce::jlimit (-1.0f, 1.0f, pan.load (std::memory_order_relaxed));
    const float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    const float leftGain = std::cos (angle) * g;
    const float rightGain = std::sin (angle) * g;

    juce::FloatVectorOperations::addWithMultiply (master.getWritePointer (0), work.getReadPointer (0), leftGain, numSamples);
    juce::FloatVectorOperations::addWithMultiply (master.getWritePointer (1), work.getReadPointer (1), rightGain, numSamples);
}
