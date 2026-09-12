#include "AudioEngine.h"
#include <algorithm>
#include <cmath>

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

    {
        const juce::ScopedLock sl (midiLock);
        pendingMidi.clear();
    }

    midiAllScratch.clear();
    for (int b = 0; b < numMidiDeviceBuckets; ++b)
        midiDeviceBuckets[b].clear();
    numMidiDeviceBuckets = 0;

    juce::MidiBuffer panicMidi;
    buildPanicMidi (panicMidi);

    for (auto& track : tracks_)
        track->plugins.deliverMidiPanic (panicMidi, numSamples);

    masterPlugins_.deliverMidiPanic (panicMidi, numSamples);
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
    trackExecutor.ensureStarted();
    prepareGraph();
    {
        const juce::ScopedLock midi (midiLock);
        pendingMidi.clear();
    }

    // Soft-start: interface open / floating inputs often click for a few ms.
    constexpr double fadeSeconds = 0.05;
    outputFadeSamplesRemaining = juce::jmax (1, (int) std::lround (sampleRate * fadeSeconds));
    masterBus.clear();
    masterPeak = 0.0f;
    gateEnv = 0.0f;
    gateGain = 0.0f;
    gateOpen = false;
    resetPeakLimiter();
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
    gateEnv = 0.0f;
    gateGain = 1.0f;
    gateOpen = true;
    resetPeakLimiter();
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

    // ~2 ms look-ahead keeps live latency small while avoiding instant-clip harshness.
    limiterLookAhead = juce::jlimit (1, 512, (int) std::lround (sampleRate * 0.002));
    limiterDelay.setSize (2, limiterLookAhead, false, true, true);
    limiterDelay.clear();
    limiterPeakRing.allocate ((size_t) limiterLookAhead, true);
    limiterWrite = 0;
    limiterGain = 1.0f;
    limiterWindowPeak = 0.0f;
    const float sr = sampleRate > 0.0 ? (float) sampleRate : 48000.0f;
    limiterAttack = 1.0f - std::exp (-1.0f / (0.0005f * sr));  // ~0.5 ms
    limiterRelease = 1.0f - std::exp (-1.0f / (0.080f * sr));   // ~80 ms
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

void AudioEngine::clearTracksAndMaster()
{
    tracks_.clear();
    trackCount.store (0, std::memory_order_relaxed);
    masterPlugins_.clear();
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
    gateEnv = 0.0f;
    gateGain = 1.0f;
    gateOpen = true;
    masterPeak = 0.0f;
    resetPeakLimiter();
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

void AudioEngine::applyGate (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const float sr = sampleRate > 0.0 ? (float) sampleRate : 48000.0f;
    const float openLin = juce::Decibels::decibelsToGain (gateThresholdDb.load (std::memory_order_relaxed));
    const float closeLin = openLin * 0.63f; // ~4 dB hysteresis
    const float attack = 1.0f - std::exp (-1.0f / (0.003f * sr));
    const float release = 1.0f - std::exp (-1.0f / (0.080f * sr));
    const float envRel = 1.0f - std::exp (-1.0f / (0.020f * sr));

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    for (int i = 0; i < numSamples; ++i)
    {
        const float peak = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        if (peak > gateEnv)
            gateEnv = peak;
        else
            gateEnv += (peak - gateEnv) * envRel;

        if (gateOpen)
        {
            if (gateEnv < closeLin)
                gateOpen = false;
        }
        else if (gateEnv >= openLin)
        {
            gateOpen = true;
        }

        const float target = gateOpen ? 1.0f : 0.0f;
        gateGain += (target - gateGain) * (gateOpen ? attack : release);

        if (gateGain <= 1.0e-4f)
        {
            left[i] = 0.0f;
            if (right != left)
                right[i] = 0.0f;
        }
        else if (gateGain < 0.999f)
        {
            left[i] *= gateGain;
            if (right != left)
                right[i] *= gateGain;
        }
    }
}

void AudioEngine::resetPeakLimiter() noexcept
{
    limiterGain = 1.0f;
    limiterWrite = 0;
    limiterWindowPeak = 0.0f;
    if (limiterDelay.getNumSamples() > 0)
        limiterDelay.clear();
    if (limiterPeakRing != nullptr && limiterLookAhead > 0)
        juce::FloatVectorOperations::clear (limiterPeakRing.getData(), limiterLookAhead);
}

void AudioEngine::applyPeakLimiter (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (numSamples <= 0 || limiterLookAhead <= 0 || limiterDelay.getNumSamples() < limiterLookAhead
        || limiterPeakRing == nullptr)
        return;

    const float ceiling = juce::Decibels::decibelsToGain (limiterThresholdDb.load (std::memory_order_relaxed));
    if (ceiling <= 0.0f)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    auto* delayL = limiterDelay.getWritePointer (0);
    auto* delayR = limiterDelay.getWritePointer (1);
    auto* peakRing = limiterPeakRing.getData();

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right[i];

        const float outL = delayL[limiterWrite];
        const float outR = delayR[limiterWrite];

        delayL[limiterWrite] = inL;
        delayR[limiterWrite] = inR;

        const float samplePeak = juce::jmax (std::abs (inL), std::abs (inR));
        const float oldPeak = peakRing[limiterWrite];
        peakRing[limiterWrite] = samplePeak;

        // Sliding-window max: O(1) when rising / when old wasn't the max; rescan only when needed.
        if (samplePeak >= limiterWindowPeak)
        {
            limiterWindowPeak = samplePeak;
        }
        else if (oldPeak >= limiterWindowPeak * 0.999f)
        {
            float peak = 0.0f;
            for (int j = 0; j < limiterLookAhead; ++j)
                peak = juce::jmax (peak, peakRing[j]);
            limiterWindowPeak = peak;
        }

        const float target = limiterWindowPeak > ceiling ? (ceiling / limiterWindowPeak) : 1.0f;
        const float coeff = target < limiterGain ? limiterAttack : limiterRelease;
        limiterGain += (target - limiterGain) * coeff;

        left[i] = outL * limiterGain;
        if (right != left)
            right[i] = outR * limiterGain;

        limiterWrite = (limiterWrite + 1) % limiterLookAhead;
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
    static const juce::MidiBuffer emptyMidi;
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

        const juce::MidiBuffer* trackMidi = &emptyMidi;
        if (track->midiDeviceId == midiAllDevicesId)
            trackMidi = &midiAllScratch;
        else if (track->midiDeviceId.isNotEmpty())
        {
            if (auto* found = findDeviceMidi (track->midiDeviceId))
                trackMidi = found;
        }

        jobs[numJobs++] = { track.get(), trackMidi };
    }

    trackExecutor.processAndMix (jobs, numJobs, inputChannelData, numInputChannels, numSamples, masterBus,
                                 parallelTracksEnabled.load (std::memory_order_relaxed));
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
        applyGate (masterBus, numSamples);

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
        applyPeakLimiter (masterBus, numSamples);
    }
    else
    {
        const float peak = masterBus.getMagnitude (0, numSamples);
        if (peak > 1.0f)
        {
            auto* left = masterBus.getWritePointer (0);
            auto* right = masterBus.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = juce::jlimit (-1.0f, 1.0f, left[i]);
                right[i] = juce::jlimit (-1.0f, 1.0f, right[i]);
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
