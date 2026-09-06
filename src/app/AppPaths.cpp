#include "AppPaths.h"

juce::File AppPaths::appDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LiteHost");
    dir.createDirectory();
    return dir;
}

juce::File AppPaths::settingsFile()
{
    return appDirectory().getChildFile ("settings.xml");
}

juce::File AppPaths::audioFile()
{
    return appDirectory().getChildFile ("audio.xml");
}

juce::File AppPaths::knownPluginsFile()
{
    return appDirectory().getChildFile ("knownPlugins.xml");
}

juce::File AppPaths::pluginLoadLogFile()
{
    return appDirectory().getChildFile ("plugin_load.log");
}

juce::File AppPaths::deadMansPedalFile()
{
    return appDirectory().getChildFile ("deadMansPedal");
}

juce::File AppPaths::legacySessionFile()
{
    return appDirectory().getChildFile ("session.xml");
}

juce::File AppPaths::defaultProjectsDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("LiteHost");
    dir.createDirectory();
    return dir;
}
