#include "UpdateChecker.h"

namespace
{
    constexpr const char* kOwner = "XiAce-Lite";
    constexpr const char* kRepo = "LiteHost";
}

UpdateChecker::UpdateChecker()
    : Thread ("LiteHost update check")
{
}

UpdateChecker::~UpdateChecker()
{
    stopThread (4000);
}

void UpdateChecker::start (juce::String currentVersionIn,
                           juce::String skippedTagIn,
                           Callback callbackIn)
{
    if (isThreadRunning())
        return;

    currentVersion = std::move (currentVersionIn);
    skippedTag = std::move (skippedTagIn);
    callback = std::move (callbackIn);
    startThread();
}

juce::StringArray UpdateChecker::versionParts (juce::String text)
{
    text = text.trim();
    if (text.startsWithChar ('v') || text.startsWithChar ('V'))
        text = text.substring (1);

    juce::StringArray parts;
    parts.addTokens (text, ".", "");
    while (parts.size() < 3)
        parts.add ("0");
    return parts;
}

bool UpdateChecker::isNewerVersion (const juce::String& latest, const juce::String& current)
{
    const auto a = versionParts (latest);
    const auto b = versionParts (current);
    const int n = juce::jmin (a.size(), b.size());
    for (int i = 0; i < n; ++i)
    {
        const int av = a[i].getIntValue();
        const int bv = b[i].getIntValue();
        if (av > bv)
            return true;
        if (av < bv)
            return false;
    }
    return false;
}

void UpdateChecker::run()
{
    Result result;

    juce::URL url ("https://api.github.com/repos/" + juce::String (kOwner) + "/"
                   + juce::String (kRepo) + "/releases/latest");

    auto stream = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders ("User-Agent: LiteHost\r\nAccept: application/vnd.github+json\r\n")
            .withConnectionTimeoutMs (8000));
    if (threadShouldExit() || stream == nullptr)
        return;

    const auto body = stream->readEntireStreamAsString();
    const auto parsed = juce::JSON::parse (body);
    if (auto* obj = parsed.getDynamicObject())
    {
        result.tag = obj->getProperty ("tag_name").toString();
        result.htmlUrl = obj->getProperty ("html_url").toString();
        result.version = versionParts (result.tag).joinIntoString (".");
    }

    if (result.tag.isEmpty())
        return;

    if (result.tag == skippedTag)
        return;

    result.newer = isNewerVersion (result.tag, currentVersion);
    if (! result.newer || callback == nullptr)
        return;

    juce::MessageManager::callAsync ([cb = callback, result] { cb (result); });
}
