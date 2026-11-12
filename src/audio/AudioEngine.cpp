#include "AudioEngine.h"
#include <algorithm>
#include <cmath>

AudioEngine::AudioEngine()
{
    masterBus.setSize (2, maxBlockSize, false, true, true);
    playHead.setPlaying (true);
    masterPlugins_.setPlayHead (&playHead);
    buildPanicMidi (panicMidi);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    midiRouter.push (source != nullptr ? source->getIdentifier() : juce::String(), message);
}

void AudioEngine::setTrackMidiDevice (TrackProcessor& track, const juce::String& deviceId)
{
    track.midiDeviceId = deviceId;

    int route = MidiInputRouter::routeNone;
    if (deviceId == midiAllDevicesId)
    {
        route = MidiInputRouter::routeAll;
    }
    else if (deviceId.isNotEmpty())
    {
        route = midiRouter.slotForDevice (deviceId, [this] (int slot) {
            return std::any_of (tracks_.begin(), tracks_.end(), [slot] (const auto& t) {
                return t->getMidiRoute() == slot;
            });
        });
    }

    track.midiRoute.store (route, std::memory_order_relaxed);
}

void AudioEngine::requestPanic() noexcept
{
    panicPending.store (true, std::memory_order_release);
}

void AudioEngine::buildPanicMidi (juce::MidiBuffer& dest)
{
    dest.clear();
    for (int channel = 1; channel <= 16; ++channel)
    {
        dest.addEvent (juce::MidiMessage::controllerEvent (channel, 64, 0), 0);   // sustain off
        dest.addEvent (juce::MidiMessage::controllerEvent (channel, 120, 0), 0);  // all sound off
        dest.addEvent (juce::MidiMessage::controllerEvent (channel, 123, 0), 0);  // all notes off
        for (int note = 0; note < 128; ++note)
            dest.addEvent (juce::MidiMessage::noteOff (channel, note), 0);
    }
}

void AudioEngine::applyPanicIfNeeded (int numSamples) noexcept
{
    if (! panicPending.exchange (false, std::memory_order_acq_rel))
        return;

    midiRouter.discardPending();
    midiRouter.clearBlockBuffers();

    for (auto& track : tracks_)
        track->plugins.deliverMidiPanic (panicMidi, numSamples);

    masterPlugins_.deliverMidiPanic (panicMidi, numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (callbackLock);
    sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    playHead.setSampleRate (sampleRate);
    playHead.setPlaying (true);
    playHead.resetPosition();
    trackExecutor.ensureStarted();
    prepareGraph();
    midiRouter.discardPending();

    // Soft-start: interface open / floating inputs often click for a few ms.
    constexpr double fadeSeconds = 0.05;
    outputFadeSamplesRemaining = juce::jmax (1, (int) std::lround (sampleRate * fadeSeconds));
    masterBus.clear();
    masterPeak = 0.0f;
    gate.reset (true);
    limiter.reset();
    reverb.reset();
    running = true;

    // Honour panic pressed while the device was stopped.
    applyPanicIfNeeded (blockSize);
}

void AudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock sl (callbackLock);
    running = false;
    outputFadeSamplesRemaining = 0;
    playHead.setPlaying (false);

    for (auto& track : tracks_)
        track->release();

    masterPlugins_.release();
    reverb.reset();
    gate.reset (false);
    limiter.reset();
    // Keep workers alive across device restarts; destroy with the engine.
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
    limiter.prepare (sampleRate);
    gate.prepare (sampleRate);
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

void AudioEngine::removeTrack (const juce::Uuid& id, DetachedObjects& detached)
{
    auto it = std::find_if (tracks_.begin(), tracks_.end(),
                            [&id] (const auto& track) { return track->id == id; });
    if (it == tracks_.end())
        return;

    detached.add (std::move (*it));
    tracks_.erase (it);
    trackCount.store ((int) tracks_.size(), std::memory_order_relaxed);
}

bool AudioEngine::hasSoloOverride() const noexcept
{
    for (auto& track : tracks_)
        if (track->soloOverride.load (std::memory_order_relaxed))
            return true;

    return false;
}

void AudioEngine::setTrackSolo (TrackProcessor& track, bool shouldSolo)
{
    if (shouldSolo && exclusiveSoloMode.load (std::memory_order_relaxed))
    {
        for (auto& other : tracks_)
            if (other.get() != &track)
                other->solo.store (false, std::memory_order_relaxed);
    }

    track.solo.store (shouldSolo, std::memory_order_relaxed);
}

bool AudioEngine::moveTrack (int fromIndex, int destIndex)
{
    const int n = (int) tracks_.size();
    if (! juce::isPositiveAndBelow (fromIndex, n))
        return false;

    destIndex = juce::jlimit (0, n - 1, destIndex);
    if (fromIndex == destIndex)
        return false;

    auto item = std::move (tracks_[(size_t) fromIndex]);
    tracks_.erase (tracks_.begin() + fromIndex);
    tracks_.insert (tracks_.begin() + destIndex, std::move (item));
    return true;
}

void AudioEngine::clearTracksAndMaster (DetachedObjects& detached)
{
    for (auto& track : tracks_)
        detached.add (std::move (track));
    tracks_.clear();
    trackCount.store (0, std::memory_order_relaxed);
    masterPlugins_.clear (detached);

    reverbEnabled = false;
    limiterEnabled = false;
    gateEnabled = false;
    reverbWet = 0.18f;
    reverbRoom = 0.42f;
    reverbDamping = 0.4f;
    reverbWidth = 1.0f;
    limiterThresholdDb = -0.3f;
    gateThresholdDb = -52.0f;
    masterGain = 1.0f;
    gate.reset (false);
    masterPeak = 0.0f;
    limiter.reset();
    appliedReverbRoom = -1.0f;
    appliedReverbDamping = -1.0f;
    appliedReverbWet = -1.0f;
    appliedReverbWidth = -1.0f;
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

void AudioEngine::applyOutputFadeIn (int numSamples) noexcept
{
    if (outputFadeSamplesRemaining <= 0 || numSamples <= 0)
        return;

    const int fadeTotal = juce::jmax (1, (int) std::lround (sampleRate * 0.05));
    const int remainingAtStart = outputFadeSamplesRemaining;
    const int apply = juce::jmin (numSamples, remainingAtStart);

    auto* left = masterBus.getWritePointer (0);
    auto* right = masterBus.getNumChannels() > 1 ? masterBus.getWritePointer (1) : left;

    for (int i = 0; i < apply; ++i)
    {
        const int elapsed = fadeTotal - remainingAtStart + i;
        const float t = juce::jlimit (0.0f, 1.0f, (float) elapsed / (float) fadeTotal);
        // Raised cosine: quieter start than a linear ramp against interface pops.
        const float g = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
        left[i] *= g;
        if (right != left)
            right[i] *= g;
    }

    outputFadeSamplesRemaining = remainingAtStart - apply;
}

void AudioEngine::processActiveTracks (const float* const* inputChannelData,
                                       int numInputChannels,
                                       int numSamples,
                                       bool anySolo) noexcept
{
    ParallelTrackExecutor::Job jobs[maxParallelTrackJobs];
    int numJobs = 0;

    for (auto& track : tracks_)
    {
        if (numJobs >= maxParallelTrackJobs)
            break;
        if (track->mute.load (std::memory_order_relaxed))
            continue;
        if (anySolo
            && ! track->solo.load (std::memory_order_relaxed)
            && ! track->soloOverride.load (std::memory_order_relaxed))
            continue;

        jobs[numJobs++] = { track.get(), &midiRouter.bufferForRoute (track->getMidiRoute()) };
    }

    const bool parallel = parallelTracksEnabled.load (std::memory_order_relaxed);

    // Longest-first scheduling: the heaviest track starts immediately instead of finishing last.
    if (parallel && numJobs > 2)
    {
        for (int i = 1; i < numJobs; ++i)
        {
            const auto job = jobs[i];
            const auto cost = job.track->lastProcessTicks.load (std::memory_order_relaxed);
            int j = i - 1;
            while (j >= 0 && jobs[j].track->lastProcessTicks.load (std::memory_order_relaxed) < cost)
            {
                jobs[j + 1] = jobs[j];
                --j;
            }
            jobs[j + 1] = job;
        }
    }

    trackExecutor.processAndMix (jobs, numJobs, inputChannelData, numInputChannels, numSamples, masterBus, parallel);
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
    midiRouter.drain (numSamples, sampleRate);
    applyPanicIfNeeded (numSamples);

    bool anySolo = false;
    for (auto& track : tracks_)
    {
        if (track->solo.load (std::memory_order_relaxed))
        {
            anySolo = true;
            break;
        }
    }

    processActiveTracks (inputChannelData, numInputChannels, numSamples, anySolo);

    if (gateEnabled.load (std::memory_order_relaxed))
        gate.process (masterBus, numSamples, gateThresholdDb.load (std::memory_order_relaxed));

    if (reverbEnabled.load (std::memory_order_relaxed))
    {
        auto block = juce::dsp::AudioBlock<float> (masterBus).getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    const float masterG = masterGain.load (std::memory_order_relaxed);
    if (! juce::approximatelyEqual (masterG, 1.0f))
        masterBus.applyGain (0, numSamples, masterG);

    // Peak-limit before master VSTs so SyncRoom (etc.) receives the limited mix.
    if (limiterEnabled.load (std::memory_order_relaxed))
    {
        limiter.process (masterBus, numSamples, limiterThresholdDb.load (std::memory_order_relaxed));
    }
    else
    {
        const float peak = masterBus.getMagnitude (0, numSamples);
        if (peak > 1.0f)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                auto* data = masterBus.getWritePointer (channel);
                juce::FloatVectorOperations::clip (data, data, -1.0f, 1.0f, numSamples);
            }
        }
    }

    static const juce::MidiBuffer emptyMidi;
    masterPlugins_.process (masterBus, emptyMidi);

    const float magnitude = juce::jmin (1.0f, masterBus.getMagnitude (0, numSamples));
    const float previous = masterPeak.load (std::memory_order_relaxed);
    masterPeak.store (juce::jmax (previous * 0.6f, magnitude), std::memory_order_relaxed);

    applyOutputFadeIn (numSamples);
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
