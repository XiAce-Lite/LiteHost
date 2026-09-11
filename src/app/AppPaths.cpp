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

juce::File AppPaths::pluginScannerExecutable()
{
    const auto host = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    return host.getSiblingFile ("LiteHostScanner.exe");
   #else
    return host.getSiblingFile ("LiteHostScanner");
   #endif
}

void AppPaths::preparePluginScannerForLaunch()
{
   #if JUCE_MAC
    const auto scanner = pluginScannerExecutable();
    if (! scanner.existsAsFile())
        return;

    // GitHub zip downloads get com.apple.quarantine; Gatekeeper then SIGKILLs
    // ChildProcess helpers even after the user opens the main app.
    const auto appBundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const juce::StringArray targets {
        appBundle.getFullPathName(),
        scanner.getFullPathName()
    };

    for (const auto& path : targets)
    {
        if (path.isEmpty())
            continue;

        juce::ChildProcess proc;
        juce::StringArray args;
        args.add ("/usr/bin/xattr");
        args.add ("-dr");
        args.add ("com.apple.quarantine");
        args.add (path);
        if (proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            proc.waitForProcessToFinish (5000);
    }

    // Ensure execute bit survived zip/copy.
    {
        juce::ChildProcess chmodProc;
        juce::StringArray args;
        args.add ("/bin/chmod");
        args.add ("+x");
        args.add (scanner.getFullPathName());
        if (chmodProc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            chmodProc.waitForProcessToFinish (2000);
    }
   #endif
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
