#pragma once

#include <JuceHeader.h>

/** TCP line protocol between LiteHost and LiteHostScanner (UTF-8).
    Scanner prints "PORT <n>" on stdout, then serves 127.0.0.1:<n>.
    Request:  SCAN\t<path>\n  |  QUIT\n
    Reply:    OK\t<base64(xml)>\n  |  FAIL\t<reason>\n
    XML root is <TYPES> with zero or more PluginDescription elements. */
namespace PluginScanIpc
{
    inline constexpr const char* portPrefix = "PORT ";
    inline constexpr const char* scanPrefix = "SCAN\t";
    inline constexpr const char* quitCommand = "QUIT";
    inline constexpr const char* okPrefix = "OK\t";
    inline constexpr const char* failPrefix = "FAIL\t";
    inline constexpr const char* typesTag = "TYPES";

    inline juce::String encodeTypesXml (const juce::OwnedArray<juce::PluginDescription>& types)
    {
        juce::XmlElement root (typesTag);
        for (auto* type : types)
        {
            if (type == nullptr)
                continue;
            if (auto xml = type->createXml())
                root.addChildElement (xml.release());
        }
        return juce::Base64::toBase64 (root.toString());
    }

    inline bool decodeTypesXml (const juce::String& base64,
                                juce::OwnedArray<juce::PluginDescription>& result)
    {
        juce::MemoryOutputStream raw;
        if (! juce::Base64::convertFromBase64 (raw, base64))
            return false;

        auto xml = juce::XmlDocument::parse (raw.toString());
        if (xml == nullptr || ! xml->hasTagName (typesTag))
            return false;

        for (auto* child : xml->getChildIterator())
        {
            auto desc = std::make_unique<juce::PluginDescription>();
            if (desc->loadFromXml (*child))
                result.add (desc.release());
        }
        return true;
    }

    inline juce::String makeScanRequest (const juce::String& fileOrIdentifier)
    {
        return juce::String (scanPrefix) + fileOrIdentifier;
    }

    inline juce::String makeOkReply (const juce::OwnedArray<juce::PluginDescription>& types)
    {
        return juce::String (okPrefix) + encodeTypesXml (types);
    }

    inline juce::String makeFailReply (const juce::String& reason)
    {
        return juce::String (failPrefix) + reason;
    }
}
