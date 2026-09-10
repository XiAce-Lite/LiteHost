#include "PluginScanThread.h"
#include "Utf8.h"
#include "app/AppPaths.h"
#include "app/OutOfProcessPluginScanner.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    void persistPluginList (juce::KnownPluginList& list)
    {
        if (auto xml = list.createXml())
            xml->writeTo (AppPaths::knownPluginsFile());
    }

    int chooseWorkerCount() noexcept
    {
        const auto hw = (int) std::thread::hardware_concurrency();
        // Cap at 4: each worker is a full JUCE VST host process.
        return juce::jlimit (2, 4, hw > 0 ? hw / 2 : 2);
    }

    juce::StringArray collectScanTargets (juce::AudioPluginFormat& format,
                                          const juce::FileSearchPath& paths,
                                          juce::KnownPluginList& list,
                                          const juce::File& deadMansPedal)
    {
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (list, deadMansPedal);

        auto files = format.searchPathsForPlugins (paths, true, true);

        // Previously-crashed plugs go last so healthy ones register first.
        juce::StringArray crashed;
        deadMansPedal.readLines (crashed);
        crashed.removeEmptyStrings();
        for (const auto& c : crashed)
        {
            const int idx = files.indexOf (c);
            if (idx >= 0)
                files.move (idx, -1);
        }

        juce::StringArray targets;
        targets.ensureStorageAllocated (files.size());

        for (const auto& file : files)
        {
            if (file.isEmpty())
                continue;
            if (list.getBlacklistedFiles().contains (file))
                continue;
            if (! OutOfProcessPluginScanner::isLikelyCompatibleVst3 (file))
                continue;
            if (list.isListingUpToDate (file, format))
                continue;

            targets.add (file);
        }

        return targets;
    }
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
    const auto scannerExe = AppPaths::pluginScannerExecutable();
    if (! scannerExe.existsAsFile())
    {
        auto* hostPtr = &owner;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                          hostPtr] {
            if (safe != nullptr)
            {
                hostPtr->setScanStatus (jp (u8"LiteHostScanner が見つかりません"));
                hostPtr->scanFinished (0);
            }
        });
        return;
    }

    const auto deadMansPedal = AppPaths::deadMansPedalFile();
    const auto targets = collectScanTargets (format, paths, list, deadMansPedal);
    const int total = targets.size();

    if (total == 0)
    {
        persistPluginList (list);
        auto* hostPtr = &owner;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                          hostPtr] {
            if (safe != nullptr)
                hostPtr->scanFinished (0);
        });
        return;
    }

    const int workers = juce::jmin (chooseWorkerCount(), total);
    std::atomic<int> nextIndex { 0 };
    std::atomic<int> completed { 0 };
    std::atomic<int> failed { 0 };
    std::mutex listMutex;

    auto* hostPtr = &owner;
    auto* safeComp = owner.asComponent();

    auto publishStatus = [hostPtr, safeComp] (const juce::String& text) {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (safeComp),
                                          hostPtr, text] {
            if (safe != nullptr)
                hostPtr->setScanStatus (text);
        });
    };

    publishStatus (jp (u8"並列スキャン開始: ") + juce::String (total) + jp (u8" 件 / ")
                   + juce::String (workers) + jp (u8" ワーカー"));

    std::vector<std::thread> threads;
    threads.reserve ((size_t) workers);

    for (int w = 0; w < workers; ++w)
    {
        threads.emplace_back ([&, w] {
            juce::ignoreUnused (w);
            OutOfProcessPluginScanner scanner (scannerExe);
            scanner.setWaitTickHandler ([publishStatus] (const juce::String& file, int waitedMs, int timeoutMs) {
                const auto shortName = juce::File (file).getFileName();
                const int left = juce::jmax (0, (timeoutMs - waitedMs + 999) / 1000);
                publishStatus (jp (u8"応答待ち ") + juce::String (left) + jp (u8"秒…  ") + shortName);
            });

            for (;;)
            {
                if (threadShouldExit())
                    break;

                const int index = nextIndex.fetch_add (1);
                if (index >= total)
                    break;

                const auto path = targets[index];
                const auto shortName = juce::File (path).getFileName();

                publishStatus (jp (u8"スキャン中 (")
                               + juce::String (completed.load()) + "/" + juce::String (total)
                               + ")  " + shortName);

                {
                    const std::lock_guard<std::mutex> lock (listMutex);
                    juce::StringArray pedal;
                    deadMansPedal.readLines (pedal);
                    pedal.removeEmptyStrings();
                    pedal.removeString (path);
                    pedal.add (path);
                    deadMansPedal.replaceWithText (pedal.joinIntoString ("\n"), true, true);
                }

                juce::OwnedArray<juce::PluginDescription> found;
                const auto outcome = scanner.scanFile (path, found);

                {
                    const std::lock_guard<std::mutex> lock (listMutex);

                    if (outcome == OutOfProcessPluginScanner::Outcome::failed)
                    {
                        list.addToBlacklist (path);
                        failed.fetch_add (1);
                    }
                    else
                    {
                        for (auto* desc : found)
                            if (desc != nullptr)
                                list.addType (*desc);
                    }

                    persistPluginList (list);

                    juce::StringArray pedal;
                    deadMansPedal.readLines (pedal);
                    pedal.removeString (path);
                    deadMansPedal.replaceWithText (pedal.joinIntoString ("\n"), true, true);
                }

                const int done = completed.fetch_add (1) + 1;
                const int percent = juce::jlimit (0, 100, (done * 100) / juce::jmax (1, total));
                if (outcome == OutOfProcessPluginScanner::Outcome::failed)
                    publishStatus (jp (u8"スキップ (") + juce::String (percent) + "%)  " + shortName);
                else
                    publishStatus (jp (u8"スキャン中 (") + juce::String (percent) + "%)  " + shortName);
            }

            scanner.shutdown();
        });
    }

    for (auto& th : threads)
        th.join();

    persistPluginList (list);

    const int failCount = failed.load();
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (owner.asComponent()),
                                      hostPtr, failCount] {
        if (safe != nullptr)
            hostPtr->scanFinished (failCount);
    });
}
