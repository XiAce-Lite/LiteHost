#include "OutOfProcessPluginScanner.h"
#include "scanner/PluginScanIpc.h"

#include <atomic>
#include <thread>

OutOfProcessPluginScanner::OutOfProcessPluginScanner (juce::File scannerExecutable)
    : scannerExe (std::move (scannerExecutable))
{
}

OutOfProcessPluginScanner::~OutOfProcessPluginScanner()
{
    stopChild (true);
}

void OutOfProcessPluginScanner::shutdown()
{
    stopChild (true);
}

bool OutOfProcessPluginScanner::warmup()
{
    return ensureChild();
}

void OutOfProcessPluginScanner::killChildNow()
{
    socket.reset();

    if (child != nullptr && child->isRunning())
        child->kill();

    child.reset();
    stdoutBuffer.reset();
}

void OutOfProcessPluginScanner::stopChild (bool polite)
{
    if (polite && socket != nullptr && socket->isConnected())
        writeLine (PluginScanIpc::quitCommand);

    socket.reset();

    if (child != nullptr)
    {
        if (polite && child->isRunning())
            child->waitForProcessToFinish (500);

        if (child->isRunning())
            child->kill();
    }

    child.reset();
    stdoutBuffer.reset();
}

bool OutOfProcessPluginScanner::isLikelyCompatibleVst3 (const juce::String& fileOrIdentifier)
{
    const juce::File file (fileOrIdentifier);
    if (! file.isDirectory() || ! file.hasFileExtension (".vst3"))
        return true;

    const auto contents = file.getChildFile ("Contents");
    if (! contents.isDirectory())
        return true;

   #if JUCE_WINDOWS && JUCE_64BIT
    if (contents.getChildFile ("x86_64-win").isDirectory())
        return true;
    if (contents.getChildFile ("arm64ec-win").isDirectory())
        return true;
    if (contents.getChildFile ("x86-win").isDirectory())
        return false;
   #elif JUCE_WINDOWS
    if (contents.getChildFile ("x86-win").isDirectory())
        return true;
    if (contents.getChildFile ("x86_64-win").isDirectory())
        return false;
   #endif

    return true;
}

bool OutOfProcessPluginScanner::readChildStdoutLine (juce::String& line, int timeoutMs)
{
    line.clear();

    // JUCE ChildProcess::readProcessOutput blocks until the requested byte count
    // arrives OR the child exits. Asking for a large buffer deadlocks us: the
    // scanner only prints "PORT n\n" (~12 bytes) then waits on the TCP accept.
    // Read 1 byte at a time, and kill the child if startup exceeds the timeout
    // so a hung juceInit / Gatekeeper stall cannot freeze the scan thread forever.
    std::atomic<bool> finished { false };
    juce::ChildProcess* const proc = child.get();
    const int timeout = juce::jmax (1, timeoutMs);

    std::thread watchdog ([this, &finished, proc, timeout] {
        int lastTickSec = -1;
        for (int elapsed = 0; elapsed < timeout && ! finished.load (std::memory_order_relaxed); elapsed += 50)
        {
            juce::Thread::sleep (50);
            const int sec = (elapsed + 50) / 1000;
            if (onWaitTick && sec != lastTickSec && sec > 0)
            {
                lastTickSec = sec;
                onWaitTick ("startup", sec * 1000, timeout);
            }
        }

        if (! finished.load (std::memory_order_relaxed) && proc != nullptr && proc->isRunning())
            proc->kill();
    });

    bool ok = false;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeout;

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
                ok = true;
                break;
            }
        }

        if (ok)
            break;

        if (child == nullptr || ! child->isRunning())
            break;

        char byte = 0;
        const int got = child->readProcessOutput (&byte, 1);
        if (got > 0)
        {
            stdoutBuffer.append (&byte, 1);
            continue;
        }

        juce::Thread::sleep (5);
    }

    finished.store (true, std::memory_order_relaxed);
    watchdog.join();
    return ok;
}

bool OutOfProcessPluginScanner::ensureChild()
{
    if (child != nullptr && child->isRunning()
        && socket != nullptr && socket->isConnected())
        return true;

    stopChild (false);

    if (! scannerExe.existsAsFile())
        return false;

    child = std::make_unique<juce::ChildProcess>();
    // StringArray keeps paths with spaces intact (String overload tokenizes on spaces).
    juce::StringArray launchArgs;
    launchArgs.add (scannerExe.getFullPathName());
    if (! child->start (launchArgs, juce::ChildProcess::wantStdOut))
    {
        child.reset();
        return false;
    }

    juce::String portLine;
    if (! readChildStdoutLine (portLine, startupTimeoutMs)
        || ! portLine.startsWith (PluginScanIpc::portPrefix))
    {
        killChildNow();
        return false;
    }

    const int port = portLine.fromFirstOccurrenceOf (PluginScanIpc::portPrefix, false, false)
                         .trim()
                         .getIntValue();
    if (port <= 0)
    {
        killChildNow();
        return false;
    }

    socket = std::make_unique<juce::StreamingSocket>();
    if (! socket->connect ("127.0.0.1", port, startupTimeoutMs))
    {
        killChildNow();
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

bool OutOfProcessPluginScanner::readLine (juce::String& line, int timeoutMs, const juce::String& waitingFor)
{
    line.clear();
    if (socket == nullptr)
        return false;

    juce::MemoryOutputStream buffer;
    const auto start = juce::Time::getMillisecondCounter();
    const auto deadline = start + (juce::uint32) juce::jmax (1, timeoutMs);
    int lastTickSec = -1;

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (child != nullptr && ! child->isRunning())
            return false;

        const int waited = (int) (juce::Time::getMillisecondCounter() - start);
        const int sec = waited / 1000;
        if (onWaitTick && sec != lastTickSec)
        {
            lastTickSec = sec;
            onWaitTick (waitingFor, waited, timeoutMs);
        }

        const int ready = socket->waitUntilReady (true, 50);
        if (ready < 0)
            return false;
        if (ready == 0)
            continue;

        char chunk[512];
        const int got = socket->read (chunk, (int) sizeof (chunk), false);
        if (got < 0)
            return false;
        if (got == 0)
        {
            if (! socket->isConnected())
                return false;
            continue;
        }

        for (int i = 0; i < got; ++i)
        {
            const char c = chunk[i];
            if (c == '\n')
            {
                line = buffer.toString().trimEnd();
                return true;
            }
            if (c != '\r')
                buffer.writeByte ((juce::uint8) c);
        }
    }

    return false;
}

OutOfProcessPluginScanner::Outcome OutOfProcessPluginScanner::scanFile (
    const juce::String& fileOrIdentifier,
    juce::OwnedArray<juce::PluginDescription>& result)
{
    result.clear();

    if (! isLikelyCompatibleVst3 (fileOrIdentifier))
        return Outcome::ok; // empty — incompatible, not a crash

    if (! scannerExe.existsAsFile())
        return Outcome::failed;

    if (! ensureChild())
        return Outcome::failed;

    if (! writeLine (PluginScanIpc::makeScanRequest (fileOrIdentifier)))
    {
        killChildNow();
        return Outcome::failed;
    }

    juce::String reply;
    if (! readLine (reply, perPluginTimeoutMs, fileOrIdentifier))
    {
        killChildNow();
        return Outcome::failed;
    }

    if (reply.startsWith (PluginScanIpc::okPrefix))
    {
        const auto payload = reply.fromFirstOccurrenceOf (PluginScanIpc::okPrefix, false, false);
        if (! PluginScanIpc::decodeTypesXml (payload, result))
        {
            result.clear();
            killChildNow();
            return Outcome::failed;
        }
        return Outcome::ok;
    }

    return Outcome::failed;
}
