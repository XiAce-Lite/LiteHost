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

    /** macOS: strip Gatekeeper quarantine so LiteHostScanner can be spawned.
        Safe no-op on other platforms / if xattr is unavailable. */
    void preparePluginScannerForLaunch();
}
