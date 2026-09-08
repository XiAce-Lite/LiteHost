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

class PluginChipBar : public juce::Component
{
public:
    std::function<void (int)> onOpen;
    std::function<void (int)> onRemove;
    std::function<void (int, bool)> onBypassChanged;
    /** Return true if a drag-and-drop operation was started. */
    std::function<bool (int, juce::Component&)> onDragStart;

    void setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags);
    int getPreferredHeight() const;
    void resized() override;

private:
    class Chip final : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        Chip (PluginChipBar& ownerIn, int indexIn, juce::String nameIn, bool activeIn);

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;

        bool isActive() const noexcept { return active; }

    private:
        PluginChipBar& owner;
        int index = 0;
        juce::String name;
        bool active = true;
        bool dragStarted = false;
        bool suppressClick = false;
    };

    juce::OwnedArray<Chip> chips;
    juce::OwnedArray<juce::TextButton> removeButtons;
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

    void setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags);
    void resized() override;

private:
    void layoutChips();

    juce::Viewport viewport;
    PluginChipBar chips;
};
