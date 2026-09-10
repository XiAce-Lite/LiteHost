#include "PluginScanCoordinator.h"
#include "Utf8.h"

juce::AudioPluginFormat* PluginScanCoordinator::findVst3Format (const juce::AudioPluginFormatManager& formats)
{
    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (formats.getFormat (i)->getName() == "VST3")
            return formats.getFormat (i);

    return formats.getNumFormats() > 0 ? formats.getFormat (0) : nullptr;
}

juce::FileSearchPath PluginScanCoordinator::defaultPaths (const juce::AudioPluginFormatManager& formats)
{
    juce::FileSearchPath paths;

   #if JUCE_WINDOWS
    juce::ignoreUnused (formats);
    // 64-bit host: only the native Program Files Common VST3 folder.
    // Program Files (x86) holds 32-bit plugs that cannot load here and only waste scan time.
    paths.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::windowsProgramFilesCommon)
                                    .getChildFile ("VST3"));
   #elif JUCE_MAC
    juce::ignoreUnused (formats);
    paths.addIfNotAlreadyThere (juce::File ("/Library/Audio/Plug-Ins/VST3"));
   #else
    if (auto* format = findVst3Format (formats))
    {
        const auto all = format->getDefaultLocationsToSearch();
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        for (int p = 0; p < all.getNumPaths(); ++p)
        {
            const auto& dir = all[p];
            if (! dir.isAChildOf (home) && dir != home)
                paths.addIfNotAlreadyThere (dir);
        }
    }
   #endif

    return paths;
}

juce::FileSearchPath PluginScanCoordinator::buildPaths (const juce::AudioPluginFormatManager& formats,
                                                        const juce::StringArray& extraFolders)
{
    auto paths = defaultPaths (formats);
    for (const auto& extra : extraFolders)
        paths.addIfNotAlreadyThere (juce::File (extra));
    return paths;
}

juce::String PluginScanCoordinator::defaultFolderHint()
{
   #if JUCE_WINDOWS
    return jp (u8"標準は 64bit の Common Files\\VST3 のみ（x86 フォルダは対象外）。ユーザーフォルダは追加してください。");
   #elif JUCE_MAC
    return jp (u8"標準は /Library/Audio/Plug-Ins/VST3 のみ。ユーザー領域（~/Library/...）は追加してください。");
   #else
    return jp (u8"標準はシステムの VST3 フォルダのみ。ユーザー領域は追加してください。");
   #endif
}
