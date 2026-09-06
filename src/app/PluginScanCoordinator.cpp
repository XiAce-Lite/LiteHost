#include "PluginScanCoordinator.h"

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
    paths.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::windowsProgramFilesCommon)
                                    .getChildFile ("VST3"));
    paths.addIfNotAlreadyThere (juce::File ("C:\\Program Files (x86)\\Common Files\\VST3"));
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
