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
        return juce::jlimit (2, 4, hw > 0 ? hw / 2 : 2);
    }

    struct CollectStats
    {
        juce::StringArray targets;
        int foundOnDisk = 0;
        int skippedBlacklist = 0;
        int skippedIncompatible = 0;
        int skippedUpToDate = 0;
    };

    CollectStats collectScanTargets (juce::AudioPluginFormat& format,
                                     const juce::FileSearchPath& paths,
                                     juce::KnownPluginList& list,
                                     const juce::File& deadMansPedal)
    {
        CollectStats stats;
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (list, deadMansPedal);

        auto files = format.searchPathsForPlugins (paths, true, true);
        stats.foundOnDisk = files.size();

        juce::StringArray crashed;
        deadMansPedal.readLines (crashed);
        crashed.removeEmptyStrings();
        for (const auto& c : crashed)
        {
            const int idx = files.indexOf (c);
            if (idx >= 0)
                files.move (idx, -1);
        }

        stats.targets.ensureStorageAllocated (files.size());

        for (const auto& file : files)
        {
            if (file.isEmpty())
                continue;
            if (list.getBlacklistedFiles().contains (file))
            {
                ++stats.skippedBlacklist;
                continue;
            }
            if (! OutOfProcessPluginScanner::isLikelyCompatibleVst3 (file))
            {
                ++stats.skippedIncompatible;
                continue;
            }
            if (list.isListingUpToDate (file, format))
            {
                ++stats.skippedUpToDate;
                continue;
            }

            stats.targets.add (file);
        }

        return stats;
    }

    juce::String formatProgress (int done, int total, const juce::String& name)
    {
        const int percent = juce::jlimit (0, 100, (done * 100) / juce::jmax (1, total));
        return juce::String (done) + "/" + juce::String (total)
             + " (" + juce::String (percent) + "%)  " + name;
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
    auto* hostPtr = &owner;
    auto* safeComp = owner.asComponent();

    auto publishStatus = [hostPtr, safeComp] (const juce::String& text) {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (safeComp),
                                          hostPtr, text] {
            if (safe != nullptr)
                hostPtr->setScanStatus (text);
        });
    };

    auto finish = [hostPtr, safeComp] (MixerStripHost::ScanFinishInfo info) {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (safeComp),
                                          hostPtr, info] {
            if (safe != nullptr)
                hostPtr->scanFinished (info);
        });
    };

    const auto scannerExe = AppPaths::pluginScannerExecutable();
    if (! scannerExe.existsAsFile())
    {
        publishStatus (jp (u8"LiteHostScanner が見つかりません"));
        finish ({});
        return;
    }

    publishStatus (jp (u8"スキャナ起動を確認しています…"));
    AppPaths::preparePluginScannerForLaunch();

    {
        OutOfProcessPluginScanner probe (scannerExe);
        if (! probe.warmup())
        {
            publishStatus (jp (u8"スキャナを起動できませんでした"));
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (safeComp)] {
                if (safe == nullptr)
                    return;
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "LiteHost",
                    jp (u8"LiteHostScanner を起動できませんでした。\n\n"
                        u8"macOS で GitHub の zip から展開した場合、Gatekeeper が子プロセスを止めることがあります。\n"
                        u8"ターミナルで次を実行してから、もう一度スキャンしてください。\n\n"
                        u8"xattr -cr ./LiteHost.app\n\n"
                        u8"また、フォルダ名にスペースが多い場所（例: \"Mac SSD 1\"）も失敗しやすいので、"
                        u8"/Applications などへ移して試してください。"));
            });
            finish ({});
            return;
        }
        probe.shutdown();
    }

    publishStatus (jp (u8"フォルダを検索しています…（件数が多いと時間がかかります）"));

    const auto deadMansPedal = AppPaths::deadMansPedalFile();
    const auto collected = collectScanTargets (format, paths, list, deadMansPedal);
    const int total = collected.targets.size();

    {
        juce::String msg = jp (u8"検索完了: ディスク ") + juce::String (collected.foundOnDisk) + jp (u8" 件");
        msg += jp (u8" / 今回 ") + juce::String (total) + jp (u8" 件");
        if (collected.skippedBlacklist > 0)
            msg += jp (u8" / ブラックリスト ") + juce::String (collected.skippedBlacklist);
        if (collected.skippedUpToDate > 0)
            msg += jp (u8" / 既存 ") + juce::String (collected.skippedUpToDate);
        if (collected.skippedIncompatible > 0)
            msg += jp (u8" / 非対応 ") + juce::String (collected.skippedIncompatible);
        publishStatus (msg);
        juce::Thread::sleep (400); // let the user actually read the summary line
    }

    if (total == 0)
    {
        persistPluginList (list);
        MixerStripHost::ScanFinishInfo info;
        info.registeredTotal = list.getNumTypes();
        info.skippedBlacklist = collected.skippedBlacklist;
        info.skippedUpToDate = collected.skippedUpToDate;
        info.skippedIncompatible = collected.skippedIncompatible;
        info.foundOnDisk = collected.foundOnDisk;
        finish (info);
        return;
    }

    const int workers = juce::jmin (chooseWorkerCount(), total);
    publishStatus (jp (u8"並列スキャン開始: ") + juce::String (total) + jp (u8" 件 / ")
                   + juce::String (workers) + jp (u8" ワーカー"));

    std::atomic<int> nextIndex { 0 };
    std::atomic<int> completed { 0 };
    std::atomic<int> failed { 0 };
    std::atomic<int> newlyRegistered { 0 };
    std::mutex listMutex;
    const auto& targets = collected.targets;

    std::vector<std::thread> threads;
    threads.reserve ((size_t) workers);

    for (int w = 0; w < workers; ++w)
    {
        threads.emplace_back ([&, w] {
            juce::ignoreUnused (w);
            OutOfProcessPluginScanner scanner (scannerExe);
            scanner.setWaitTickHandler ([&] (const juce::String& file, int waitedMs, int timeoutMs) {
                const auto shortName = juce::File (file).getFileName();
                const int left = juce::jmax (0, (timeoutMs - waitedMs + 999) / 1000);
                const int done = completed.load();
                publishStatus (jp (u8"応答待ち ") + juce::String (left) + jp (u8"秒…  ")
                               + formatProgress (done, total, shortName));
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

                publishStatus (jp (u8"スキャン中 ")
                               + formatProgress (completed.load(), total, shortName));

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
                        int added = 0;
                        for (auto* desc : found)
                        {
                            if (desc == nullptr)
                                continue;
                            list.addType (*desc);
                            ++added;
                        }
                        if (added > 0)
                            newlyRegistered.fetch_add (added);
                    }

                    persistPluginList (list);

                    juce::StringArray pedal;
                    deadMansPedal.readLines (pedal);
                    pedal.removeString (path);
                    deadMansPedal.replaceWithText (pedal.joinIntoString ("\n"), true, true);
                }

                const int done = completed.fetch_add (1) + 1;
                if (outcome == OutOfProcessPluginScanner::Outcome::failed)
                    publishStatus (jp (u8"失敗スキップ ") + formatProgress (done, total, shortName));
                else
                    publishStatus (jp (u8"スキャン中 ") + formatProgress (done, total, shortName));
            }

            scanner.shutdown();
        });
    }

    for (auto& th : threads)
        th.join();

    persistPluginList (list);

    MixerStripHost::ScanFinishInfo info;
    info.registeredTotal = list.getNumTypes();
    info.newlyRegistered = newlyRegistered.load();
    info.failed = failed.load();
    info.skippedBlacklist = collected.skippedBlacklist;
    info.skippedUpToDate = collected.skippedUpToDate;
    info.skippedIncompatible = collected.skippedIncompatible;
    info.foundOnDisk = collected.foundOnDisk;
    info.examined = completed.load();
    finish (info);
}
