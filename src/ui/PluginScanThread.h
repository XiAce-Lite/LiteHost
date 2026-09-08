#pragma once

#include "MixerStripHost.h"
#include <JuceHeader.h>

class PluginScanThread : public juce::Thread
{
public:
    PluginScanThread (MixerStripHost& ownerIn,
                      juce::KnownPluginList& list,
                      juce::AudioPluginFormat& format,
                      juce::FileSearchPath pathsIn);

    void run() override;

private:
    MixerStripHost& owner;
    juce::KnownPluginList& list;
    juce::AudioPluginFormat& format;
    juce::FileSearchPath paths;
};
