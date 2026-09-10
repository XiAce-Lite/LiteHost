#pragma once

#include <JuceHeader.h>

/** Loads plugin metadata via LiteHostScanner child process over localhost TCP.
    A crashing / hung plugin kills only the child; the host continues. */
class OutOfProcessPluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessPluginScanner (juce::File scannerExecutable);
    ~OutOfProcessPluginScanner() override;

    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    void scanFinished() override;

private:
    bool ensureChild();
    void stopChild();
    bool writeLine (const juce::String& line);
    bool readLine (juce::String& line, int timeoutMs);
    bool readChildStdoutLine (juce::String& line, int timeoutMs);
    bool scanInProcess (juce::AudioPluginFormat& format,
                        juce::OwnedArray<juce::PluginDescription>& result,
                        const juce::String& fileOrIdentifier);

    juce::File scannerExe;
    std::unique_ptr<juce::ChildProcess> child;
    std::unique_ptr<juce::StreamingSocket> socket;
    juce::MemoryBlock stdoutBuffer;
    static constexpr int perPluginTimeoutMs = 15000;
    static constexpr int startupTimeoutMs = 15000;
};
