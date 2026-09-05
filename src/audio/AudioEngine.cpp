#include "AudioEngine.h"
#include <algorithm>
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

AudioEngine::AudioEngine()
{
    masterBus.setSize (2, maxBlockSize, false, true, true);
    playHead.setPlaying (true);
    masterPlugins_.setPlayHead (&playHead);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    auto stamped = message;
    if (stamped.getTimeStamp() == 0.0)
        stamped.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);

    const juce::String id = source != nullptr ? source->getIdentifier() : juce::String();

    const juce::ScopedLock sl (midiLock);
    pendingMidi.add ({ id, stamped });

    constexpr int maxPending = 2048;
    if (pendingMidi.size() > maxPending)
        pendingMidi.removeRange (0, pendingMidi.size() - maxPending);
}

void AudioEngine::drainPendingMidi (int numSamples) noexcept
{
    midiAllScratch.clear();
    numMidiDeviceBuckets = 0;

    {
        const juce::ScopedLock sl (midiLock);
        midiDrainBatch.swapWith (pendingMidi);
    }

    if (midiDrainBatch.isEmpty() || numSamples <= 0)
        return;

    const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double blockSec = sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0;

    auto addToDevice = [this] (const juce::String& id, const juce::MidiMessage& message, int pos) {
        for (int b = 0; b < numMidiDeviceBuckets; ++b)
        {
            if (midiDeviceBucketIds[b] == id)
            {
                midiDeviceBuckets[b].addEvent (message, pos);
                return;
            }
        }
        if (numMidiDeviceBuckets < maxMidiDeviceBuckets)
        {
            const int b = numMidiDeviceBuckets++;
            midiDeviceBucketIds[b] = id;
            midiDeviceBuckets[b].clear();
            midiDeviceBuckets[b].addEvent (message, pos);
        }
    };

    for (int i = 0; i < midiDrainBatch.size(); ++i)
    {
        auto& event = midiDrainBatch.getReference (i);
        int pos = 0;
        if (blockSec > 0.0 && event.message.getTimeStamp() > 0.0)
        {
            const double age = juce::jlimit (0.0, blockSec, nowSec - event.message.getTimeStamp());
            pos = juce::jlimit (0, numSamples - 1,
                                (int) std::lround ((blockSec - age) / blockSec * (double) (numSamples - 1)));
        }
        else if (midiDrainBatch.size() > 1)
        {
            pos = (i * (numSamples - 1)) / (midiDrainBatch.size() - 1);
        }

        midiAllScratch.addEvent (event.message, pos);
        addToDevice (event.deviceId, event.message, pos);
    }

    midiDrainBatch.clearQuick();
}

const juce::MidiBuffer* AudioEngine::findDeviceMidi (const juce::String& deviceId) const noexcept
{
    for (int b = 0; b < numMidiDeviceBuckets; ++b)
        if (midiDeviceBucketIds[b] == deviceId)
            return &midiDeviceBuckets[b];
    return nullptr;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (callbackLock);
    sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    playHead.setSampleRate (sampleRate);
    playHead.setPlaying (true);
    playHead.resetPosition();
    prepareGraph();
    {
        const juce::ScopedLock midi (midiLock);
        pendingMidi.clear();
    }
    running = true;
}

void AudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock sl (callbackLock);
    running = false;
    playHead.setPlaying (false);

    for (auto& track : tracks_)
        track->release();

    masterPlugins_.release();
    reverb.reset();
    limiter.reset();
}

void AudioEngine::prepareGraph()
{
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = 2;

    masterBus.setSize (2, maxBlockSize, false, false, true);

    masterPlugins_.setPlayHead (&playHead);
    for (auto& track : tracks_)
    {
        track->plugins.setPlayHead (&playHead);
        track->prepare (sampleRate, blockSize, &playHead);
    }

    masterPlugins_.prepare (sampleRate, blockSize, &playHead);
    reverb.prepare (spec);
    limiter.prepare (spec);
    limiter.setThreshold (limiterThresholdDb.load());
    limiter.setRelease (limiterReleaseMs.load());
}

TrackProcessor* AudioEngine::addTrack (juce::String name)
{
    auto track = std::make_unique<TrackProcessor> (std::move (name));
    track->plugins.setPlayHead (&playHead);
    if (running)
        track->prepare (sampleRate, blockSize, &playHead);

    auto* raw = track.get();
    tracks_.push_back (std::move (track));
    trackCount.store ((int) tracks_.size(), std::memory_order_relaxed);
    return raw;
}

void AudioEngine::removeTrack (const juce::Uuid& id)
{
    tracks_.erase (std::remove_if (tracks_.begin(), tracks_.end(),
                                   [&id] (const auto& track) { return track->id == id; }),
                   tracks_.end());
    trackCount.store ((int) tracks_.size(), std::memory_order_relaxed);
}

void AudioEngine::clearTracksAndMaster()
{
    tracks_.clear();
    trackCount.store (0, std::memory_order_relaxed);
    masterPlugins_.clear();
    reverbEnabled = false;
    limiterEnabled = false;
    reverbWet = 0.18f;
    reverbRoom = 0.42f;
    reverbDamping = 0.4f;
    reverbWidth = 1.0f;
    limiterThresholdDb = -0.3f;
    limiterReleaseMs = 80.0f;
    masterGain = 1.0f;
    masterPeak = 0.0f;
    appliedReverbRoom = -1.0f;
    appliedReverbDamping = -1.0f;
    appliedReverbWet = -1.0f;
    appliedReverbWidth = -1.0f;
    appliedLimiterThreshold = 1.0e6f;
    appliedLimiterRelease = -1.0f;
}

TrackProcessor* AudioEngine::findTrack (const juce::Uuid& id) const
{
    for (auto& track : tracks_)
        if (track->id == id)
            return track.get();

    return nullptr;
}

void AudioEngine::updateBuiltInParameters() noexcept
{
    const bool wantReverb = reverbEnabled.load (std::memory_order_relaxed);
    if (wantReverb)
    {
        const float room = reverbRoom.load (std::memory_order_relaxed);
        const float damping = reverbDamping.load (std::memory_order_relaxed);
        const float wet = reverbWet.load (std::memory_order_relaxed);
        const float width = reverbWidth.load (std::memory_order_relaxed);

        if (! juce::approximatelyEqual (room, appliedReverbRoom)
            || ! juce::approximatelyEqual (damping, appliedReverbDamping)
            || ! juce::approximatelyEqual (wet, appliedReverbWet)
            || ! juce::approximatelyEqual (width, appliedReverbWidth))
        {
            juce::dsp::Reverb::Parameters params;
            params.roomSize = room;
            params.damping = damping;
            params.width = width;
            params.wetLevel = wet;
            params.dryLevel = 1.0f - wet;
            params.freezeMode = 0.0f;
            reverb.setParameters (params);
            appliedReverbRoom = room;
            appliedReverbDamping = damping;
            appliedReverbWet = wet;
            appliedReverbWidth = width;
        }
    }

    const bool wantLimiter = limiterEnabled.load (std::memory_order_relaxed);
    if (wantLimiter)
    {
        const float threshold = limiterThresholdDb.load (std::memory_order_relaxed);
        const float release = limiterReleaseMs.load (std::memory_order_relaxed);

        if (! juce::approximatelyEqual (threshold, appliedLimiterThreshold)
            || ! juce::approximatelyEqual (release, appliedLimiterRelease))
        {
            limiter.setThreshold (threshold);
            limiter.setRelease (release);
            appliedLimiterThreshold = threshold;
            appliedLimiterRelease = release;
        }
    }
}

void AudioEngine::copyToOutputs (float* const* outputs, int numOutputChannels, int numSamples) noexcept
{
    if (outputs == nullptr)
        return;

    if (numOutputChannels <= 0)
        return;

    if (numOutputChannels == 1)
    {
        if (outputs[0] != nullptr)
        {
            juce::FloatVectorOperations::copy (outputs[0], masterBus.getReadPointer (0), numSamples);
            juce::FloatVectorOperations::add (outputs[0], masterBus.getReadPointer (1), numSamples);
            juce::FloatVectorOperations::multiply (outputs[0], 0.5f, numSamples);
        }
        return;
    }

    if (outputs[0] != nullptr)
        juce::FloatVectorOperations::copy (outputs[0], masterBus.getReadPointer (0), numSamples);
    if (outputs[1] != nullptr)
        juce::FloatVectorOperations::copy (outputs[1], masterBus.getReadPointer (1), numSamples);

    for (int channel = 2; channel < numOutputChannels; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputs[channel], numSamples);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const auto startTicks = juce::Time::getHighResolutionTicks();
    const juce::ScopedLock sl (callbackLock);
    juce::ScopedNoDenormals noDenormals;

    if (outputChannelData != nullptr)
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numSamples <= 0 || numSamples > maxBlockSize)
        return;

    masterBus.setSize (2, numSamples, false, false, true);
    masterBus.clear();
    updateBuiltInParameters();
    drainPendingMidi (numSamples);

    bool anySolo = false;
    for (auto& track : tracks_)
    {
        if (track->solo.load (std::memory_order_relaxed))
        {
            anySolo = true;
            break;
        }
    }

    static const juce::MidiBuffer emptyMidi;

    for (auto& track : tracks_)
    {
        if (track->mute.load (std::memory_order_relaxed))
            continue;
        if (anySolo && ! track->solo.load (std::memory_order_relaxed))
            continue;

        const juce::MidiBuffer* trackMidi = &emptyMidi;
        if (track->midiDeviceId == midiAllDevicesId)
            trackMidi = &midiAllScratch;
        else if (track->midiDeviceId.isNotEmpty())
        {
            if (auto* found = findDeviceMidi (track->midiDeviceId))
                trackMidi = found;
        }

        track->processInputs (inputChannelData, numInputChannels, numSamples, *trackMidi);
        track->mixTo (masterBus, numSamples);
    }

    if (reverbEnabled.load (std::memory_order_relaxed))
    {
        auto block = juce::dsp::AudioBlock<float> (masterBus).getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    masterPlugins_.process (masterBus, emptyMidi);

    if (limiterEnabled.load (std::memory_order_relaxed))
    {
        auto block = juce::dsp::AudioBlock<float> (masterBus).getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        limiter.process (context);
    }

    const float masterG = masterGain.load (std::memory_order_relaxed);
    if (! juce::approximatelyEqual (masterG, 1.0f))
        masterBus.applyGain (0, numSamples, masterG);

    // Cheap hard clip only when needed (tanh-every-sample was too heavy at 128-sample buffers).
    const float preClipPeak = masterBus.getMagnitude (0, numSamples);
    if (preClipPeak > 1.0f)
    {
        auto* left = masterBus.getWritePointer (0);
        auto* right = masterBus.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = juce::jlimit (-1.0f, 1.0f, left[i]);
            right[i] = juce::jlimit (-1.0f, 1.0f, right[i]);
        }
    }

    const float magnitude = juce::jmin (1.0f, preClipPeak);
    const float previous = masterPeak.load (std::memory_order_relaxed);
    masterPeak.store (juce::jmax (previous * 0.6f, magnitude), std::memory_order_relaxed);

    copyToOutputs (outputChannelData, numOutputChannels, numSamples);
    playHead.advance (numSamples);

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const auto budget = sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.001;
    const auto load = budget > 0.0 ? (float) juce::jmin (1.0, elapsed / budget) : 0.0f;
    const auto smoothed = cpuLoad.load (std::memory_order_relaxed) * 0.85f + load * 0.15f;
    cpuLoad.store (smoothed, std::memory_order_relaxed);

    // Slow decay so the status line is readable (~1–2 s hang on peaks).
    const float previousPeak = cpuPeakLoad.load (std::memory_order_relaxed);
    cpuPeakLoad.store (juce::jmax (load, previousPeak * 0.997f), std::memory_order_relaxed);
}
