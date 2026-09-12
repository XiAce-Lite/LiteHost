#pragma once

#include "control/ControlSurface.h"
#include "control/MidiLearn.h"

class SurfaceSettingsPanel : public juce::Component
{
public:
    std::function<void()> onOk;
    std::function<void()> onClose;

    explicit SurfaceSettingsPanel (ControlSurfaceManager& surfaceIn);
    void resized() override;

private:
    ControlSurfaceManager& surface;
    juce::Label title, hint, protocolLabel, inLabel, outLabel;
    juce::ToggleButton enabledToggle;
    juce::ComboBox protocolBox, inBox, outBox;
    juce::TextButton ok, close;
};

class MidiLearnSettingsPanel : public juce::Component
{
public:
    std::function<void()> onOk;
    std::function<void()> onClose;

    explicit MidiLearnSettingsPanel (MidiLearnManager& learnIn);
    void resized() override;

private:
    MidiLearnManager& learn;
    juce::Label title, hint, inLabel;
    juce::ToggleButton enabledToggle;
    juce::ComboBox inBox;
    juce::TextButton clearAll, ok, close;
};

class OptionsGeneralPanel : public juce::Component
{
public:
    std::function<void()> onOk;
    std::function<void()> onClose;

    OptionsGeneralPanel (bool exclusiveSolo, bool parallelTracks);
    void resized() override;

    bool getExclusiveSolo() const { return exclusiveSoloToggle.getToggleState(); }
    bool getParallelTracks() const { return parallelTracksToggle.getToggleState(); }

private:
    juce::Label title, exclusiveHint, parallelHint;
    juce::ToggleButton exclusiveSoloToggle, parallelTracksToggle;
    juce::TextButton ok, close;
};
