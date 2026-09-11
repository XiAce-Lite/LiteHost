#include <JuceHeader.h>
#include "PluginScanIpc.h"
#include "app/ScanUiSuppressor.h"

#include <atomic>
#include <iostream>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace
{
    juce::AudioPluginFormat* findVst3Format (juce::AudioPluginFormatManager& formats)
    {
        for (int i = 0; i < formats.getNumFormats(); ++i)
            if (formats.getFormat (i)->getName() == "VST3")
                return formats.getFormat (i);

        return formats.getNumFormats() > 0 ? formats.getFormat (0) : nullptr;
    }

    bool writeLine (juce::StreamingSocket& socket, const juce::String& line)
    {
        const auto payload = line + "\n";
        const auto* data = payload.toRawUTF8();
        const int bytes = (int) payload.getNumBytesAsUTF8();
        return socket.write (data, bytes) == bytes;
    }

    bool readLine (juce::StreamingSocket& socket, juce::String& line, int timeoutMs)
    {
        line.clear();
        juce::MemoryOutputStream buffer;
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (1, timeoutMs);

        while (juce::Time::getMillisecondCounter() < deadline)
        {
            const int ready = socket.waitUntilReady (true, 50);
            if (ready < 0)
                return false;
            if (ready == 0)
            {
                if (! socket.isConnected())
                    return false;
                continue;
            }

            char chunk[512];
            const int got = socket.read (chunk, (int) sizeof (chunk), false);
            if (got < 0)
                return false;
            if (got == 0)
            {
                if (! socket.isConnected())
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

    void abortProcessHard()
    {
       #if JUCE_WINDOWS
        ::TerminateProcess (::GetCurrentProcess(), 42);
       #else
        _exit (42);
       #endif
    }

    void scanOneOnMessageThread (juce::AudioPluginFormat& format,
                                 juce::StreamingSocket& socket,
                                 const juce::String& path)
    {
        juce::OwnedArray<juce::PluginDescription> types;
        juce::String failReason;
        std::atomic<bool> done { false };

        std::thread watchdog ([&done] {
            for (int i = 0; i < 28 && ! done.load (std::memory_order_relaxed); ++i)
                juce::Thread::sleep (100);
            if (! done.load (std::memory_order_relaxed))
                abortProcessHard();
        });

        juce::MessageManager::callSync ([&] {
            try
            {
                format.findAllTypesForFile (types, path);
            }
            catch (...)
            {
                failReason = "exception";
            }
        });

        done.store (true, std::memory_order_relaxed);
        watchdog.join();

        if (failReason.isNotEmpty())
            writeLine (socket, PluginScanIpc::makeFailReply (failReason));
        else
            writeLine (socket, PluginScanIpc::makeOkReply (types));
    }

    void serveClient (juce::AudioPluginFormat& format,
                      juce::StreamingSocket& client,
                      std::atomic<bool>& finished)
    {
        const ScanUiSuppressor suppressPluginUi;

        juce::String line;
        while (readLine (client, line, 120000))
        {
            if (line == PluginScanIpc::quitCommand)
                break;

            if (line.startsWith (PluginScanIpc::scanPrefix))
            {
                const auto path = line.fromFirstOccurrenceOf (PluginScanIpc::scanPrefix, false, false);
                if (path.isNotEmpty())
                    scanOneOnMessageThread (format, client, path);
                else
                    writeLine (client, PluginScanIpc::makeFailReply ("empty-path"));
                continue;
            }

            writeLine (client, PluginScanIpc::makeFailReply ("bad-request"));
        }

        finished.store (true);
        juce::MessageManager::callAsync ([] {});
    }
}

int main (int, char**)
{
   #if JUCE_WINDOWS
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
   #endif

    // Unbuffered stdout so PORT reaches the parent even when piped.
    std::cout.setf (std::ios::unitbuf);

    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Process::setPriority (juce::Process::NormalPriority);
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();

    // Announce readiness BEFORE loading plugin formats (can be slow on macOS).
    juce::StreamingSocket listener;
    if (! listener.createListener (0, "127.0.0.1"))
        return 2;

    const int port = listener.getBoundPort();
    if (port <= 0)
        return 3;

    std::cout << PluginScanIpc::portPrefix << port << std::endl;
    std::cout.flush();

    std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
    if (client == nullptr || ! client->isConnected())
        return 4;

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    auto* format = findVst3Format (formats);
    if (format == nullptr)
    {
        writeLine (*client, PluginScanIpc::makeFailReply ("no-vst3-format"));
        return 1;
    }

    std::atomic<bool> finished { false };
    std::thread worker ([&] {
        serveClient (*format, *client, finished);
    });

    while (! finished.load())
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    worker.join();
    return 0;
}
