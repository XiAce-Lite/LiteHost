#pragma once

#include <JuceHeader.h>

class LevelMeter : public juce::Component
{
public:
    void setLevel (float linear);
    void tick (float deltaMs);
    void paint (juce::Graphics& g) override;

private:
    float level = 0.0f;
    float hold = 0.0f;
    float holdMs = 0.0f;
};

/** Compact power-style toggle for track-wide VST chain bypass. */
class ChainPowerButton : public juce::Button
{
public:
    ChainPowerButton();

    void paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override;
};

class PluginChipBar : public juce::Component,
                      public juce::DragAndDropTarget
{
public:
    std::function<void (int)> onOpen;
    std::function<void (int)> onRemove;
    std::function<void (int, bool)> onBypassChanged;
    /** Return true if a drag-and-drop operation was started. */
    std::function<bool (int, juce::Component&)> onDragStart;
    /** Drop a plugin drag at insertIndex (0 = top, size = bottom). */
    std::function<void (int insertIndex, const juce::var& description)> onPluginDrop;

    void setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags);
    /** Visual only: dim individually-on chips while the whole chain is bypassed. */
    void setChainBypassed (bool shouldBypass);
    bool isChainBypassed() const noexcept { return chainBypassed; }
    /** Hover help under the (optional) truncated name; track vs master differ. */
    void setChipHelpText (juce::String text);
    int getPreferredHeight() const;
    void paint (juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

private:
    class Chip final : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        Chip (PluginChipBar& ownerIn, int indexIn, juce::String nameIn, bool activeIn);

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        bool keyPressed (const juce::KeyPress& key) override;
        void focusGained (juce::Component::FocusChangeType cause) override;
        void focusLost (juce::Component::FocusChangeType cause) override;

        bool isActive() const noexcept { return active; }
        void updateTooltip();

    private:
        PluginChipBar& owner;
        int index = 0;
        juce::String name;
        bool active = true;
        bool dragStarted = false;
        bool suppressClick = false;
    };

    int insertIndexFromY (int y) const noexcept;
    static bool isPluginDrag (const SourceDetails& details) noexcept;

    juce::OwnedArray<Chip> chips;
    juce::OwnedArray<juce::TextButton> removeButtons;
    int dropInsertIndex = -1;
    bool chainBypassed = false;
    juce::String chipHelpText;
};

/** Plugin chips with a vertical scrollbar when the list is taller than the slot. */
class PluginChipList : public juce::Component
{
public:
    PluginChipList();

    std::function<void (int)> onOpen;
    std::function<void (int)> onRemove;
    std::function<void (int, bool)> onBypassChanged;
    /** Return true if a drag-and-drop operation was started. */
    std::function<bool (int, juce::Component&)> onDragStart;
    std::function<void (int insertIndex, const juce::var& description)> onPluginDrop;

    void setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags);
    void setChainBypassed (bool shouldBypass);
    void setChipHelpText (juce::String text);
    void resized() override;

private:
    void layoutChips();

    juce::Viewport viewport;
    PluginChipBar chips;
};
