#pragma once

#include <JuceHeader.h>

class AudioEngine;
class ControlSurfaceManager;
class MidiLearnManager;

class AppSettingsStore
{
public:
    juce::File currentProject;
    juce::Array<juce::File> recentProjects;
    juce::StringArray extraVstPaths;
    juce::String windowState;
    juce::String skippedReleaseTag;
    bool setupWizardCompleted = false;

    void load (AudioEngine& engine,
               ControlSurfaceManager& surface,
               MidiLearnManager& learn);
    void save (const AudioEngine& engine,
               const ControlSurfaceManager& surface,
               const MidiLearnManager& learn) const;

    void rememberProject (const juce::File& file);
    void clearRecent();
};
