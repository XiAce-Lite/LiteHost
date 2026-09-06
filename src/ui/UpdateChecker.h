#pragma once

#include <JuceHeader.h>

class UpdateChecker : private juce::Thread
{
public:
    struct Result
    {
        bool newer = false;
        juce::String tag;
        juce::String version;
        juce::String htmlUrl;
    };

    using Callback = std::function<void (Result)>;

    UpdateChecker();
    ~UpdateChecker() override;

    void start (juce::String currentVersion,
                juce::String skippedTag,
                Callback callback);

    static bool isNewerVersion (const juce::String& latest, const juce::String& current);
    static juce::StringArray versionParts (juce::String text);

private:
    void run() override;

    juce::String currentVersion;
    juce::String skippedTag;
    Callback callback;
};
