#pragma once

#include "audio/AudioEngine.h"
#include "control/ControlSurface.h"
#include "control/MidiLearn.h"

class MixerSession : public ControlSurfaceListener,
                     public MidiLearnListener
{
public:
    class Host
    {
    public:
        virtual ~Host() = default;
        virtual bool isAudioEngineRunning() const = 0;
        virtual void setAudioEngineRunning (bool shouldRun) = 0;
        virtual void controlSurfaceBankChanged (int bankOffset) = 0;
        virtual void midiLearnFinished (bool assigned) = 0;
        virtual void mixerUiChanged() = 0;
        virtual void projectEdited() = 0;
    };

    MixerSession (AudioEngine& engineToUse, Host& hostToUse);

    AudioEngine& getEngine() noexcept { return engine; }
    const AudioEngine& getEngine() const noexcept { return engine; }

    TrackProcessor* trackAt (int index) const;
    int indexOfTrack (const TrackProcessor& track) const;
    void applySoloClick (const juce::Uuid& trackId, bool shift);

    bool getExclusiveSoloMode() const;
    void setExclusiveSoloMode (bool enabled);
    bool getParallelTracksEnabled() const;
    void setParallelTracksEnabled (bool enabled);

    void setTrackGain (TrackProcessor& track, float gainLinear, bool notifyUi = false);
    void setTrackTrim (TrackProcessor& track, float gainLinear, bool notifyUi = false);
    void setTrackPan (TrackProcessor& track, float pan, bool notifyUi = false);
    void setTrackMute (TrackProcessor& track, bool mute, bool notifyUi = false);

    void setReverbEnabled (bool enabled, bool notifyUi);
    void setReverbMix (float wet, bool notifyUi);
    void setReverbSize (float size, bool notifyUi);
    void setLimiterEnabled (bool enabled, bool notifyUi);
    void setLimiterCeilingDb (float db, bool notifyUi);
    void setGateEnabled (bool enabled, bool notifyUi);
    void setGateThresholdDb (float db, bool notifyUi);
    void setMasterGain (float gainLinear, bool notifyUi);

    int getNumTracks() const override;
    float getTrackGain (int trackIndex) const override;
    float getTrackPan (int trackIndex) const override;
    bool getTrackMute (int trackIndex) const override;
    bool getTrackSolo (int trackIndex) const override;
    float getMasterGain() const override;
    bool isAudioEngineRunning() const override;
    void setTrackGain (int trackIndex, float gainLinear) override;
    void setTrackPan (int trackIndex, float pan) override;
    void setTrackMute (int trackIndex, bool mute) override;
    void setTrackSolo (int trackIndex, bool solo) override;
    void setMasterGain (float gainLinear) override;
    void setAudioEngineRunning (bool shouldRun) override;
    void controlSurfaceBankChanged (int bankOffset) override;

    void setTrackTrim (int trackIndex, float gainLinear) override;
    bool getReverbEnabled() const override;
    void setReverbEnabled (bool enabled) override;
    void setReverbMix (float wet) override;
    void setReverbSize (float size) override;
    bool getLimiterEnabled() const override;
    void setLimiterEnabled (bool enabled) override;
    void setLimiterCeilingDb (float db) override;
    bool getGateEnabled() const override;
    void setGateEnabled (bool enabled) override;
    void setGateThresholdDb (float db) override;
    void midiLearnFinished (bool assigned) override;

private:
    void notifyUiIfNeeded (bool notifyUi);

    AudioEngine& engine;
    Host& host;
};
