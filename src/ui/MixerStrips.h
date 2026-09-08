#pragma once

#include "MixerWidgets.h"
#include "audio/AudioEngine.h"
#include "control/MidiLearn.h"

class MainComponent;

class TrackStrip : public juce::Component,
                   public juce::DragAndDropTarget
{
public:
    static constexpr int stripWidth = 184;
    static constexpr const char* dragType = "litehost-track";
    static constexpr const char* pluginDragType = "litehost-plugin";

    TrackStrip (MainComponent& ownerIn, TrackProcessor& trackIn);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    void refreshInputs();
    void refreshPlugins();
    void setPeak (float value);
    juce::Uuid getTrackId() const;
    void syncFromTrack();

private:
    bool isTrackDragSource (const juce::Component* component) const noexcept;
    void updateSoloButton();
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
    juce::Component dragGrip;
    juce::ComboBox input;
    juce::TextButton mute, solo, addFx, remove;
    juce::Slider gain, pan, trim;
    PluginChipList chips;
    LevelMeter meter;
    bool dropBefore = false;
    bool dropAfter = false;
    bool pluginDropHighlight = false;
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
    struct LearnClickListener : public juce::MouseListener
    {
        MasterStrip& strip;
        explicit LearnClickListener (MasterStrip& s) : strip (s) {}
        void mouseDown (const juce::MouseEvent& e) override;
    };

    MainComponent& owner;
    LearnClickListener learnClicks { *this };
    juce::Label title, reverbMixLabel, reverbSizeLabel, limitLabel, gateLabel, masterGainLabel;
    juce::TextButton reverbToggle, limiterToggle, gateToggle, addFx;
    juce::Slider reverbMix, reverbSize, limitCeiling, gateThreshold, masterGain;
    PluginChipList chips;
    LevelMeter meter;
};
