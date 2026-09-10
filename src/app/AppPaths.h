#pragma once

#include <JuceHeader.h>

namespace AppPaths
{
    juce::File appDirectory();
    juce::File settingsFile();
    juce::File audioFile();
    juce::File knownPluginsFile();
    juce::File pluginLoadLogFile();
    juce::File deadMansPedalFile();
    juce::File pluginScannerExecutable();
    juce::File legacySessionFile();
    juce::File defaultProjectsDirectory();
}
