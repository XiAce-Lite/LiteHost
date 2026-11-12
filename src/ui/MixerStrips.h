#pragma once

#include "MixerWidgets.h"
#include "MixerStripHost.h"
#include "audio/TrackProcessor.h"
#include "control/MidiLearn.h"

class TrackStrip : public juce::Component,
                   public juce::DragAndDropTarget
{
public:
    static constexpr int stripWidth = 184;
    /** Send knob floor; at or below this the send is off. */
    static constexpr float sendOffDb = -60.0f;
    static constexpr const char* dragType = "litehost-track";
    static constexpr const char* pluginDragType = "litehost-plugin";

    TrackStrip (MixerStripHost& ownerIn, TrackProcessor& trackIn);

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
    TrackProcessor& getTrack() const noexcept { return track; }
    void syncFromTrack();

private:
    bool isTrackDragSource (const juce::Component* component) const noexcept;
    void updateSoloButton();
    void createSendKnobs();
    void updateSendLabel (int index);
    void showSendMenu (int index);

    MixerStripHost& owner;
    TrackProcessor& track;
    struct LearnClickListener : public juce::MouseListener
    {
        TrackStrip& strip;
        explicit LearnClickListener (TrackStrip& s) : strip (s) {}
        void mouseDown (const juce::MouseEvent& e) override;
    };
    LearnClickListener learnClicks { *this };
    juce::Label name, panLabel, trimLabel;
    /** Return strips: fixed "from send X" caption in place of the input combo. */
    juce::Label sourceLabel;
    juce::Component dragGrip;
    juce::ComboBox input;
    juce::TextButton mute, solo, addFx, remove;
    ChainPowerButton vstToggle;
    juce::Slider gain, pan, trim;
    /** One per return bus; empty on return strips. */
    juce::OwnedArray<juce::Slider> sendKnobs;
    juce::OwnedArray<juce::Label> sendLabels;
    PluginChipList chips;
    LevelMeter meter;
    bool dropBefore = false;
    bool dropAfter = false;
    bool pluginDropHighlight = false;
};

/** Thin handle between tracks and return buses. Click toggles the return area; meters show while collapsed. */
class ReturnsBar : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    static constexpr int barWidth = 22;

    std::function<void()> onToggle;

    void setState (int numReturns, bool isCollapsed);
    void setPeak (int index, float value);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    juce::OwnedArray<LevelMeter> meters;
    bool collapsed = false;
};

class MasterStrip : public juce::Component
{
public:
    explicit MasterStrip (MixerStripHost& ownerIn);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refreshPlugins();
    void syncTogglesFromEngine();
    void setPeak (float value);

private:
    MixerStripHost& owner;
    struct LearnClickListener : public juce::MouseListener
    {
        MasterStrip& strip;
        explicit LearnClickListener (MasterStrip& s) : strip (s) {}
        void mouseDown (const juce::MouseEvent& e) override;
    };
    LearnClickListener learnClicks { *this };
    juce::Label title, reverbMixLabel, reverbSizeLabel, limitLabel, gateLabel, masterGainLabel;
    juce::TextButton reverbToggle, limiterToggle, gateToggle, addFx;
    juce::Slider reverbMix, reverbSize, limitCeiling, gateThreshold, masterGain;
    PluginChipList chips;
    LevelMeter meter;
};
