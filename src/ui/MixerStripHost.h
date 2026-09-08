#pragma once

#include "control/MidiLearn.h"
#include <JuceHeader.h>

class AudioEngine;
class MixerSession;
class TrackProcessor;
class TrackStrip;

/**
 * Narrow façade for mixer strips and plugin scan UI callbacks.
 * MainComponent implements this; strips/scan no longer include MainComponent.h.
 */
class MixerStripHost
{
public:
    virtual ~MixerStripHost() = default;

    virtual juce::AudioDeviceManager& getDeviceManager() noexcept = 0;
    virtual AudioEngine& getEngine() noexcept = 0;
    virtual MixerSession& getMixer() noexcept = 0;
    virtual int indexOfTrack (const TrackProcessor& track) const = 0;

    virtual void markProjectDirty() = 0;
    virtual void syncTrackMidiInputs() = 0;
    virtual void applySoloClick (const juce::Uuid& trackId, bool shift) = 0;
    virtual void promptAddPlugin (const juce::Uuid& trackId, bool master) = 0;
    virtual void openPluginEditor (juce::AudioPluginInstance& plugin) = 0;
    virtual void removePluginFromTrack (const juce::Uuid& trackId, int index) = 0;
    virtual void removePluginFromMaster (int index) = 0;
    virtual void removeTrack (const juce::Uuid& id) = 0;
    virtual void showLearnMenuForTrack (int trackIndex, MidiLearnTarget target) = 0;
    virtual void beginTrackDrag (TrackStrip& strip) = 0;
    virtual void beginPluginDrag (const juce::Uuid& trackId, int pluginIndex, juce::Component& source) = 0;
    virtual void beginMasterPluginDrag (int pluginIndex, juce::Component& source) = 0;
    virtual void transferPlugin (const juce::Uuid& fromTrackId, int pluginIndex,
                                 const juce::Uuid& toTrackId, int insertIndex) = 0;
    virtual void reorderMasterPlugin (int pluginIndex, int insertIndex) = 0;
    virtual void reorderTrack (const juce::Uuid& fromId, const juce::Uuid& targetId, bool placeAfter) = 0;

    virtual void setScanStatus (const juce::String& text) = 0;
    virtual void scanFinished (int failedCount = 0) = 0;
    /** For SafePointer from background scan thread; typically the MainComponent itself. */
    virtual juce::Component* asComponent() noexcept = 0;
};
