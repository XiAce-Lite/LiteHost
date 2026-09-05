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
