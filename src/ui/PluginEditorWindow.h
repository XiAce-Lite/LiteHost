#pragma once

#include <JuceHeader.h>

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow (juce::AudioPluginInstance& instance,
                        std::function<void (PluginEditorWindow*)> onCloseIn);
    ~PluginEditorWindow() override;

    juce::AudioPluginInstance* getPlugin() const noexcept { return plugin; }
    void closeButtonPressed() override;

private:
    juce::AudioPluginInstance* plugin = nullptr;
    std::function<void (PluginEditorWindow*)> onClose;
};
