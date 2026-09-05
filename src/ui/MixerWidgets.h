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

    void setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags);
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    int getPreferredHeight() const;
    void resized() override;

private:
    juce::StringArray names;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::OwnedArray<juce::TextButton> removeButtons;
};
