#pragma once

#include "audio/PluginChain.h"
#include <functional>
#include <vector>

class AudioEngine;
class StartupProgress;

class ProjectStore
{
public:
    using LoadPluginFn = std::function<PluginChain::PluginLoadResult (PluginChain&,
                                                                      const PluginChain::PluginLoadRequest&,
                                                                      bool)>;

    static int countPlugins (const juce::XmlElement& root);

    /** Restore mixer attributes and plugin chains. Caller closes/restarts audio and arms SyncRoom. */
    static bool loadIntoEngine (const juce::XmlElement& root,
                                AudioEngine& engine,
                                int& trackSerial,
                                std::vector<juce::AudioPluginInstance*>& delayedArm,
                                const LoadPluginFn& loadPlugin,
                                StartupProgress* progress);

    static bool saveToFile (const juce::File& file, const AudioEngine& engine);
};
