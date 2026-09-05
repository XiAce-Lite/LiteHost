#pragma once

#include <JuceHeader.h>

class MainComponent;

class PluginScanThread : public juce::Thread
{
public:
    PluginScanThread (MainComponent& ownerIn,
                      juce::KnownPluginList& list,
                      juce::AudioPluginFormat& format,
                      juce::FileSearchPath pathsIn);

    void run() override;

private:
    MainComponent& owner;
    juce::KnownPluginList& list;
    juce::AudioPluginFormat& format;
    juce::FileSearchPath paths;
};
