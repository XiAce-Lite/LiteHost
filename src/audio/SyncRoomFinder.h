#pragma once

#include <JuceHeader.h>

class PluginChain;

namespace SyncRoomFinder
{
    bool isSyncRoomName (const juce::String& name);
    bool isPreferredDescription (const juce::PluginDescription& type);
    bool isBridge2 (const juce::PluginDescription& type);
    bool needsDelayedArm (const juce::PluginDescription& type);
    bool needsDelayedArm (const juce::AudioPluginInstance& plugin);
    bool shouldKeepPrepared (const juce::AudioPluginInstance& plugin);
    bool chainContains (const PluginChain& chain);

    /** Search known plugins, then scan folders, then well-known install paths. */
    bool findDescription (const juce::KnownPluginList& known,
                          const juce::AudioPluginFormatManager& formats,
                          const juce::FileSearchPath& scanPaths,
                          juce::PluginDescription& out);
}
