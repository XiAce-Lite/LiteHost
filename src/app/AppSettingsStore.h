#pragma once

#include <JuceHeader.h>

class AudioEngine;
class ControlSurfaceManager;
class MidiLearnManager;

class AppSettingsStore
{
public:
    juce::File currentProject;
    /** Modify only via rememberProject / clearRecent so the taskbar Jump List stays in sync. */
    juce::Array<juce::File> recentProjects;
    juce::StringArray extraVstPaths;
    juce::String windowState;
    juce::String skippedReleaseTag;
    bool setupWizardCompleted = false;
    bool returnsCollapsed = false;

    void load (AudioEngine& engine,
               ControlSurfaceManager& surface,
               MidiLearnManager& learn);
    void save (const AudioEngine& engine,
               const ControlSurfaceManager& surface,
               const MidiLearnManager& learn) const;

    void rememberProject (const juce::File& file);
    void clearRecent();

private:
    void loadFromFile (AudioEngine& engine,
                       ControlSurfaceManager& surface,
                       MidiLearnManager& learn);
    void recentProjectsChanged() const;
};
