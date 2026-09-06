#include "SyncRoomFinder.h"
#include "PluginChain.h"

namespace
{
    juce::AudioPluginFormat* findVst3Format (const juce::AudioPluginFormatManager& formats)
    {
        for (int i = 0; i < formats.getNumFormats(); ++i)
            if (formats.getFormat (i)->getName() == "VST3")
                return formats.getFormat (i);

        return formats.getNumFormats() > 0 ? formats.getFormat (0) : nullptr;
    }

    void considerFile (juce::AudioPluginFormat& format,
                       const juce::File& file,
                       juce::PluginDescription& bridge,
                       bool& haveBridge,
                       juce::PluginDescription& fallback,
                       bool& haveFallback)
    {
        if (! file.exists())
            return;

        juce::OwnedArray<juce::PluginDescription> foundTypes;
        format.findAllTypesForFile (foundTypes, file.getFullPathName());

        for (auto* type : foundTypes)
        {
            if (type == nullptr || ! SyncRoomFinder::isPreferredDescription (*type))
                continue;

            if (SyncRoomFinder::isBridge2 (*type))
            {
                bridge = *type;
                haveBridge = true;
                return;
            }

            if (! haveFallback)
            {
                fallback = *type;
                haveFallback = true;
            }
        }
    }
}

bool SyncRoomFinder::isSyncRoomName (const juce::String& name)
{
    return name.containsIgnoreCase ("syncroom");
}

bool SyncRoomFinder::isPreferredDescription (const juce::PluginDescription& type)
{
    if (! isSyncRoomName (type.name))
        return false;
    if (type.name.containsIgnoreCase ("multiout"))
        return false;
    return true;
}

bool SyncRoomFinder::isBridge2 (const juce::PluginDescription& type)
{
    return type.name.containsIgnoreCase ("syncroom_vst_bridge2")
        || type.fileOrIdentifier.containsIgnoreCase ("syncroom_vst_bridge2");
}

bool SyncRoomFinder::needsDelayedArm (const juce::PluginDescription& type)
{
    return isSyncRoomName (type.name);
}

bool SyncRoomFinder::needsDelayedArm (const juce::AudioPluginInstance& plugin)
{
    return isSyncRoomName (plugin.getName());
}

bool SyncRoomFinder::shouldKeepPrepared (const juce::AudioPluginInstance& plugin)
{
    return isSyncRoomName (plugin.getName());
}

bool SyncRoomFinder::chainContains (const PluginChain& chain)
{
    for (int i = 0; i < chain.size(); ++i)
        if (auto* plugin = chain.get (i))
            if (isSyncRoomName (plugin->getName()))
                return true;
    return false;
}

bool SyncRoomFinder::findDescription (const juce::KnownPluginList& known,
                                      const juce::AudioPluginFormatManager& formats,
                                      const juce::FileSearchPath& scanPaths,
                                      juce::PluginDescription& out)
{
    juce::PluginDescription bridge;
    juce::PluginDescription fallback;
    bool haveBridge = false;
    bool haveFallback = false;

    for (const auto& type : known.getTypes())
    {
        if (! isPreferredDescription (type))
            continue;

        if (isBridge2 (type))
        {
            out = type;
            return true;
        }

        if (! haveFallback)
        {
            fallback = type;
            haveFallback = true;
        }
    }

    auto* format = findVst3Format (formats);
    if (format != nullptr)
    {
        for (int p = 0; p < scanPaths.getNumPaths(); ++p)
        {
            const auto dir = scanPaths[p];
            if (! dir.isDirectory())
                continue;

            for (const auto& file : dir.findChildFiles (juce::File::findFilesAndDirectories, true, "*.vst3"))
            {
                if (! isSyncRoomName (file.getFileName()))
                    continue;

                considerFile (*format, file, bridge, haveBridge, fallback, haveFallback);
                if (haveBridge)
                {
                    out = bridge;
                    return true;
                }
            }
        }

       #if JUCE_WINDOWS
        considerFile (*format,
                      juce::File ("C:\\Program Files\\Cakewalk\\VstPlugins\\syncroom_vst_bridge2.vst3"),
                      bridge, haveBridge, fallback, haveFallback);
       #endif
    }

    if (haveBridge)
    {
        out = bridge;
        return true;
    }

    if (haveFallback)
    {
        out = fallback;
        return true;
    }

    return false;
}
