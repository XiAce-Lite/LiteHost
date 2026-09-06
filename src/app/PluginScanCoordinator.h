#pragma once

#include <JuceHeader.h>

namespace PluginScanCoordinator
{
    juce::AudioPluginFormat* findVst3Format (const juce::AudioPluginFormatManager& formats);

    /** Standard VST3 folders only. User-area paths are added via extras. */
    juce::FileSearchPath defaultPaths (const juce::AudioPluginFormatManager& formats);
    juce::FileSearchPath buildPaths (const juce::AudioPluginFormatManager& formats,
                                     const juce::StringArray& extraFolders);
}
