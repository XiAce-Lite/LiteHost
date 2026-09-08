#include "PluginScanThread.h"
#include "Utf8.h"
#include "app/AppPaths.h"
#include "app/ScanUiSuppressor.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    /** Load plugins without surfacing UI; failures blacklist silently and do not abort the scan. */
    class SilentPluginScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format,
                                 juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            try
            {
                format.findAllTypesForFile (result, fileOrIdentifier);
                return true;
            }
            catch (...)
            {
                result.clear();
                return false;
            }
        }
    };
}

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
    // IK Multimedia etc. call MessageBox on missing assets — swallow those during scan.
    const ScanUiSuppressor suppressPluginUi;

   #if JUCE_WINDOWS
    const auto previousErrorMode = SetErrorMode (SEM_FAILCRITICALERRORS
                                                 | SEM_NOGPFAULTERRORBOX
                                                 | SEM_NOOPENFILEERRORBOX);
   #endif

    list.setCustomScanner (std::make_unique<SilentPluginScanner>());

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

        auto* hostPtr = &owner;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                          hostPtr, name] {
            if (safe != nullptr)
                hostPtr->setScanStatus (jp (u8"スキャン中: ") + name);
        });
    }

    failed += scanner.getFailedFiles().size();
    list.setCustomScanner (nullptr);

   #if JUCE_WINDOWS
    SetErrorMode (previousErrorMode);
   #endif

    auto* hostPtr = &owner;
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                      hostPtr, failed] {
        if (safe != nullptr)
            hostPtr->scanFinished (failed);
    });
}
