#pragma once

#include "MixerWidgets.h"
#include "audio/AudioEngine.h"
#include "control/MidiLearn.h"

class MainComponent;

class TrackStrip : public juce::Component
{
public:
    static constexpr int stripWidth = 168;

    TrackStrip (MainComponent& ownerIn, TrackProcessor& trackIn);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refreshInputs();
    void refreshPlugins();
    void setPeak (float value);
    juce::Uuid getTrackId() const;
    void syncFromTrack();

private:
    struct LearnClickListener : public juce::MouseListener
    {
        TrackStrip& strip;
        explicit LearnClickListener (TrackStrip& s) : strip (s) {}
        void mouseDown (const juce::MouseEvent& e) override;
    };

    MainComponent& owner;
    TrackProcessor& track;
    LearnClickListener learnClicks { *this };
    juce::Label name, panLabel, trimLabel;
    juce::ComboBox input;
    juce::TextButton mute, solo, addFx, remove;
    juce::Slider gain, pan, trim;
    PluginChipBar chips;
    LevelMeter meter;
};

class MasterStrip : public juce::Component
{
public:
    explicit MasterStrip (MainComponent& ownerIn);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refreshPlugins();
    void syncTogglesFromEngine();
    void setPeak (float value);

private:
    MainComponent& owner;
    juce::Label title, reverbMixLabel, reverbSizeLabel, limitLabel, masterGainLabel;
    juce::TextButton reverbToggle, limiterToggle, addFx;
    juce::Slider reverbMix, reverbSize, limitCeiling, masterGain;
    PluginChipBar chips;
    LevelMeter meter;
};
