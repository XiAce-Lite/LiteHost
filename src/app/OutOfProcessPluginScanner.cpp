#include "OutOfProcessPluginScanner.h"
#include "scanner/PluginScanIpc.h"

OutOfProcessPluginScanner::OutOfProcessPluginScanner (juce::File scannerExecutable)
    : scannerExe (std::move (scannerExecutable))
{
}

OutOfProcessPluginScanner::~OutOfProcessPluginScanner()
{
    stopChild();
}

void OutOfProcessPluginScanner::scanFinished()
{
    stopChild();
}

void OutOfProcessPluginScanner::stopChild()
{
    if (socket != nullptr && socket->isConnected())
        writeLine (PluginScanIpc::quitCommand);

    socket.reset();

    if (child != nullptr)
    {
        if (child->isRunning())
            child->waitForProcessToFinish (2000);

        if (child->isRunning())
            child->kill();
    }

    child.reset();
    stdoutBuffer.reset();
}

bool OutOfProcessPluginScanner::readChildStdoutLine (juce::String& line, int timeoutMs)
{
    line.clear();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (1, timeoutMs);

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        const auto* data = static_cast<const char*> (stdoutBuffer.getData());
        const auto size = (int) stdoutBuffer.getSize();
        for (int i = 0; i < size; ++i)
        {
            if (data[i] == '\n')
            {
                line = juce::String::fromUTF8 (data, i).trimEnd();
                const auto remain = (size_t) (size - i - 1);
                juce::MemoryBlock next;
                if (remain > 0)
                    next.append (data + i + 1, remain);
                stdoutBuffer = std::move (next);
                return true;
            }
        }

        if (child == nullptr)
            return false;

        char chunk[256];
        const int got = child->readProcessOutput (chunk, (int) sizeof (chunk));
        if (got > 0)
        {
            stdoutBuffer.append (chunk, (size_t) got);
            continue;
        }

        if (! child->isRunning())
            return false;

        juce::Thread::sleep (5);
    }

    return false;
}

bool OutOfProcessPluginScanner::ensureChild()
{
    if (child != nullptr && child->isRunning()
        && socket != nullptr && socket->isConnected())
        return true;

    stopChild();

    if (! scannerExe.existsAsFile())
        return false;

    child = std::make_unique<juce::ChildProcess>();
    if (! child->start (scannerExe.getFullPathName(), juce::ChildProcess::wantStdOut))
    {
        child.reset();
        return false;
    }

    juce::String portLine;
    if (! readChildStdoutLine (portLine, startupTimeoutMs)
        || ! portLine.startsWith (PluginScanIpc::portPrefix))
    {
        stopChild();
        return false;
    }

    const int port = portLine.fromFirstOccurrenceOf (PluginScanIpc::portPrefix, false, false)
                         .trim()
                         .getIntValue();
    if (port <= 0)
    {
        stopChild();
        return false;
    }

    socket = std::make_unique<juce::StreamingSocket>();
    if (! socket->connect ("127.0.0.1", port, startupTimeoutMs))
    {
        stopChild();
        return false;
    }

    return true;
}

bool OutOfProcessPluginScanner::writeLine (const juce::String& line)
{
    if (socket == nullptr || ! socket->isConnected())
        return false;

    const auto payload = line + "\n";
    const auto* data = payload.toRawUTF8();
    const int bytes = (int) payload.getNumBytesAsUTF8();
    return socket->write (data, bytes) == bytes;
}

bool OutOfProcessPluginScanner::readLine (juce::String& line, int timeoutMs)
{
    line.clear();
    if (socket == nullptr)
        return false;

    juce::MemoryOutputStream buffer;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (1, timeoutMs);

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (child != nullptr && ! child->isRunning())
            return false;

        char c = 0;
        const int got = socket->read (&c, 1, false);
        if (got < 0)
            return false;
        if (got == 0)
        {
            if (! socket->isConnected())
                return false;
            juce::Thread::sleep (2);
            continue;
        }

        if (c == '\n')
        {
            line = buffer.toString().trimEnd();
            return true;
        }

        if (c != '\r')
            buffer.writeByte ((juce::uint8) c);
    }

    return false;
}

bool OutOfProcessPluginScanner::scanInProcess (juce::AudioPluginFormat& format,
                                               juce::OwnedArray<juce::PluginDescription>& result,
                                               const juce::String& fileOrIdentifier)
{
    try
    {
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }
    catch (...)
    {
        result.clear();
        return false;
    }
}

bool OutOfProcessPluginScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                                    juce::OwnedArray<juce::PluginDescription>& result,
                                                    const juce::String& fileOrIdentifier)
{
    result.clear();

    if (shouldExit())
        return false;

    if (! scannerExe.existsAsFile())
        return scanInProcess (format, result, fileOrIdentifier);

    if (! ensureChild())
        return scanInProcess (format, result, fileOrIdentifier);

    if (! writeLine (PluginScanIpc::makeScanRequest (fileOrIdentifier)))
    {
        stopChild();
        return false;
    }

    juce::String reply;
    if (! readLine (reply, perPluginTimeoutMs))
    {
        // Crash, hang, or broken pipe — blacklist this file and respawn later.
        stopChild();
        return false;
    }

    if (reply.startsWith (PluginScanIpc::okPrefix))
    {
        const auto payload = reply.fromFirstOccurrenceOf (PluginScanIpc::okPrefix, false, false);
        if (! PluginScanIpc::decodeTypesXml (payload, result))
        {
            result.clear();
            stopChild();
            return false;
        }
        return true;
    }

    return false;
}
