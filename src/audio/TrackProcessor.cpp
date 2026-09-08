#include "TrackProcessor.h"
#include "AudioEngine.h"
#include <cmath>

TrackProcessor::TrackProcessor (juce::String nameToUse)
    : name (std::move (nameToUse)),
      id (juce::Uuid())
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
    work.setSize (2, numSamples, false, false, true);
    work.clear();

    const bool useMidi = midiDeviceId.isNotEmpty();
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

    const float trimGain = trim.load (std::memory_order_relaxed);
    if (! juce::approximatelyEqual (trimGain, 1.0f))
        work.applyGain (0, numSamples, trimGain);

    const float magnitude = work.getMagnitude (0, numSamples);
    const float previous = peak.load (std::memory_order_relaxed);
    peak.store (juce::jmax (previous * 0.6f, magnitude), std::memory_order_relaxed);
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
