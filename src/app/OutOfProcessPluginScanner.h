#pragma once

#include <JuceHeader.h>
#include <functional>

/** Talks to one LiteHostScanner child over localhost TCP. */
class OutOfProcessPluginScanner
{
public:
    enum class Outcome
    {
        ok,     // probed (may still yield zero types)
        failed  // timeout / crash / soft fail — caller should blacklist
    };

    using WaitTickFn = std::function<void (const juce::String& fileOrIdentifier, int waitedMs, int timeoutMs)>;

    explicit OutOfProcessPluginScanner (juce::File scannerExecutable);
    ~OutOfProcessPluginScanner();

    void setWaitTickHandler (WaitTickFn handler) { onWaitTick = std::move (handler); }

    bool scannerAvailable() const { return scannerExe.existsAsFile(); }

    Outcome scanFile (const juce::String& fileOrIdentifier,
                      juce::OwnedArray<juce::PluginDescription>& result);

    void shutdown();

    static bool isLikelyCompatibleVst3 (const juce::String& fileOrIdentifier);

private:
    bool ensureChild();
    void stopChild (bool polite);
    void killChildNow();
    bool writeLine (const juce::String& line);
    bool readLine (juce::String& line, int timeoutMs, const juce::String& waitingFor);
    bool readChildStdoutLine (juce::String& line, int timeoutMs);

    juce::File scannerExe;
    std::unique_ptr<juce::ChildProcess> child;
    std::unique_ptr<juce::StreamingSocket> socket;
    juce::MemoryBlock stdoutBuffer;
    WaitTickFn onWaitTick;
    static constexpr int perPluginTimeoutMs = 3000;
    static constexpr int startupTimeoutMs = 8000;
};
