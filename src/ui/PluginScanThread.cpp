#include "PluginScanThread.h"
#include "Utf8.h"
#include "app/AppPaths.h"
#include "app/OutOfProcessPluginScanner.h"

PluginScanThread::PluginScanThread (MixerStripHost& ownerIn,
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
    // Plugin binaries load in LiteHostScanner; a crash kills only the child.
    list.setCustomScanner (std::make_unique<OutOfProcessPluginScanner> (AppPaths::pluginScannerExecutable()));

    const auto deadMansPedal = AppPaths::deadMansPedalFile();
    juce::PluginDirectoryScanner scanner (list, format, paths, true, deadMansPedal, true);
    juce::String name;
    int failed = 0;

    while (! threadShouldExit())
    {
        try
        {
            if (! scanner.scanNextFile (true, name))
                break;
        }
        catch (...)
        {
            // scanNextFile already advanced past this file before the throw.
            ++failed;
            name.clear();
            continue;
        }

        const int percent = juce::jlimit (0, 100, (int) std::round (scanner.getProgress() * 100.0f));
        auto* hostPtr = &owner;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                          hostPtr, name, percent] {
            if (safe != nullptr)
                hostPtr->setScanStatus (jp (u8"スキャン中 (")
                                        + juce::String (percent) + "%)  " + name);
        });
    }

    failed += scanner.getFailedFiles().size();
    list.setCustomScanner (nullptr);

    auto* hostPtr = &owner;
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                      hostPtr, failed] {
        if (safe != nullptr)
            hostPtr->scanFinished (failed);
    });
}
