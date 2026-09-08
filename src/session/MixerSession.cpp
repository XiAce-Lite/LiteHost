#include "MixerSession.h"

MixerSession::MixerSession (AudioEngine& engineToUse, Host& hostToUse)
    : engine (engineToUse),
      host (hostToUse)
{
}

TrackProcessor* MixerSession::trackAt (int index) const
{
    const juce::ScopedLock sl (engine.getCallbackLock());
    const auto& tracks = engine.tracks();
    if (! juce::isPositiveAndBelow (index, (int) tracks.size()))
        return nullptr;
    return tracks[(size_t) index].get();
}

int MixerSession::indexOfTrack (const TrackProcessor& track) const
{
    const juce::ScopedLock sl (engine.getCallbackLock());
    const auto& tracks = engine.tracks();
    for (int i = 0; i < (int) tracks.size(); ++i)
        if (tracks[(size_t) i].get() == &track)
            return i;
    return -1;
}

void MixerSession::notifyUiIfNeeded (bool notifyUi)
{
    host.projectEdited();
    if (notifyUi)
        host.mixerUiChanged();
}

void MixerSession::applySoloClick (const juce::Uuid& trackId, bool shift)
{
    if (auto* track = engine.findTrack (trackId))
    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        if (shift)
            track->soloOverride = ! track->soloOverride.load();
        else
            engine.setTrackSolo (*track, ! track->solo.load());
        host.projectEdited();
    }
}

bool MixerSession::getExclusiveSoloMode() const
{
    return engine.exclusiveSoloMode.load();
}

void MixerSession::setExclusiveSoloMode (bool enabled)
{
    engine.exclusiveSoloMode = enabled;
}

void MixerSession::setTrackGain (TrackProcessor& track, float gainLinear, bool notifyUi)
{
    track.gain = juce::jlimit (0.0f, juce::Decibels::decibelsToGain (12.0f), gainLinear);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setTrackTrim (TrackProcessor& track, float gainLinear, bool notifyUi)
{
    track.trim = juce::jlimit (0.0f, juce::Decibels::decibelsToGain (24.0f), gainLinear);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setTrackPan (TrackProcessor& track, float pan, bool notifyUi)
{
    track.pan = juce::jlimit (-1.0f, 1.0f, pan);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setTrackMute (TrackProcessor& track, bool mute, bool notifyUi)
{
    track.mute = mute;
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setReverbEnabled (bool enabled, bool notifyUi)
{
    engine.reverbEnabled = enabled;
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setReverbMix (float wet, bool notifyUi)
{
    engine.reverbWet = juce::jlimit (0.0f, 1.0f, wet);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setReverbSize (float size, bool notifyUi)
{
    engine.reverbRoom = juce::jlimit (0.0f, 1.0f, size);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setLimiterEnabled (bool enabled, bool notifyUi)
{
    engine.limiterEnabled = enabled;
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setLimiterCeilingDb (float db, bool notifyUi)
{
    engine.limiterThresholdDb = juce::jlimit (-12.0f, 0.0f, db);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setGateEnabled (bool enabled, bool notifyUi)
{
    engine.gateEnabled = enabled;
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setGateThresholdDb (float db, bool notifyUi)
{
    engine.gateThresholdDb = juce::jlimit (-80.0f, -24.0f, db);
    notifyUiIfNeeded (notifyUi);
}

void MixerSession::setMasterGain (float gainLinear, bool notifyUi)
{
    engine.masterGain = juce::jlimit (0.0f, juce::Decibels::decibelsToGain (12.0f), gainLinear);
    notifyUiIfNeeded (notifyUi);
}

int MixerSession::getNumTracks() const
{
    return engine.getTrackCount();
}

float MixerSession::getTrackGain (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->gain.load();
    return 1.0f;
}

float MixerSession::getTrackPan (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->pan.load();
    return 0.0f;
}

bool MixerSession::getTrackMute (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->mute.load();
    return false;
}

bool MixerSession::getTrackSolo (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->solo.load();
    return false;
}

float MixerSession::getMasterGain() const
{
    return engine.masterGain.load();
}

bool MixerSession::isAudioEngineRunning() const
{
    return host.isAudioEngineRunning();
}

void MixerSession::setTrackGain (int trackIndex, float gainLinear)
{
    if (auto* track = trackAt (trackIndex))
        setTrackGain (*track, gainLinear, true);
}

void MixerSession::setTrackTrim (int trackIndex, float gainLinear)
{
    if (auto* track = trackAt (trackIndex))
        setTrackTrim (*track, gainLinear, true);
}

void MixerSession::setTrackPan (int trackIndex, float pan)
{
    if (auto* track = trackAt (trackIndex))
        setTrackPan (*track, pan, true);
}

void MixerSession::setTrackMute (int trackIndex, bool mute)
{
    if (auto* track = trackAt (trackIndex))
        setTrackMute (*track, mute, true);
}

void MixerSession::setTrackSolo (int trackIndex, bool solo)
{
    if (auto* track = trackAt (trackIndex))
        engine.setTrackSolo (*track, solo);
    notifyUiIfNeeded (true);
}

void MixerSession::setMasterGain (float gainLinear)
{
    setMasterGain (gainLinear, true);
}

void MixerSession::setAudioEngineRunning (bool shouldRun)
{
    host.setAudioEngineRunning (shouldRun);
}

void MixerSession::controlSurfaceBankChanged (int bankOffset)
{
    host.controlSurfaceBankChanged (bankOffset);
}

bool MixerSession::getReverbEnabled() const
{
    return engine.reverbEnabled.load();
}

void MixerSession::setReverbEnabled (bool enabled)
{
    setReverbEnabled (enabled, true);
}

void MixerSession::setReverbMix (float wet)
{
    setReverbMix (wet, true);
}

void MixerSession::setReverbSize (float size)
{
    setReverbSize (size, true);
}

bool MixerSession::getLimiterEnabled() const
{
    return engine.limiterEnabled.load();
}

void MixerSession::setLimiterEnabled (bool enabled)
{
    setLimiterEnabled (enabled, true);
}

void MixerSession::setLimiterCeilingDb (float db)
{
    setLimiterCeilingDb (db, true);
}

bool MixerSession::getGateEnabled() const
{
    return engine.gateEnabled.load();
}

void MixerSession::setGateEnabled (bool enabled)
{
    setGateEnabled (enabled, true);
}

void MixerSession::setGateThresholdDb (float db)
{
    setGateThresholdDb (db, true);
}

void MixerSession::midiLearnFinished (bool assigned)
{
    host.midiLearnFinished (assigned);
}
