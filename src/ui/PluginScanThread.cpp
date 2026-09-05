#include "PluginScanThread.h"
#include "MainComponent.h"
#include "Utf8.h"

PluginScanThread::PluginScanThread (MainComponent& ownerIn,
                                    juce::KnownPluginList& list,
                                    juce::AudioPluginFormat& format,
                                    juce::FileSearchPath pathsIn)
    : Thread ("VST3 Scan"),
      owner (ownerIn),
      list (list),
      format (format),
      paths (std::move (pathsIn))
{
}

void PluginScanThread::run()
{
    const auto deadMansPedal = owner.getAppDir().getChildFile ("deadMansPedal");
    juce::PluginDirectoryScanner scanner (list, format, paths, true, deadMansPedal, true);
    juce::String name;

    while (! threadShouldExit() && scanner.scanNextFile (true, name))
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (&owner), name] {
            if (safe != nullptr)
                safe->setScanStatus (jp (u8"スキャン中: ") + name);
        });
    }

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (&owner)] {
        if (safe != nullptr)
            safe->scanFinished();
    });
}
