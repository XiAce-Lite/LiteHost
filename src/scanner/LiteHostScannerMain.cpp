#include <JuceHeader.h>
#include "PluginScanIpc.h"
#include "app/ScanUiSuppressor.h"

#include <iostream>

#if JUCE_WINDOWS
 #include <windows.h>
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
            char c = 0;
            const int got = socket.read (&c, 1, false);
            if (got < 0)
                return false;
            if (got == 0)
            {
                if (! socket.isConnected())
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

    void scanOne (juce::AudioPluginFormat& format,
                  juce::StreamingSocket& socket,
                  const juce::String& path)
    {
        juce::OwnedArray<juce::PluginDescription> types;

        try
        {
            format.findAllTypesForFile (types, path);
            writeLine (socket, PluginScanIpc::makeOkReply (types));
        }
        catch (...)
        {
            writeLine (socket, PluginScanIpc::makeFailReply ("exception"));
        }
    }
}

int main (int, char**)
{
   #if JUCE_WINDOWS
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
   #endif

    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Process::setPriority (juce::Process::NormalPriority);
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    auto* format = findVst3Format (formats);
    if (format == nullptr)
        return 1;

    juce::StreamingSocket listener;
    if (! listener.createListener (0, "127.0.0.1"))
        return 2;

    const int port = listener.getBoundPort();
    if (port <= 0)
        return 3;

    // Announce port on stdout for the parent ChildProcess to read.
    std::cout << PluginScanIpc::portPrefix << port << std::endl;
    std::cout.flush();

    std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
    if (client == nullptr || ! client->isConnected())
        return 4;

    const ScanUiSuppressor suppressPluginUi;

    juce::String line;
    while (readLine (*client, line, 120000))
    {
        if (line == PluginScanIpc::quitCommand)
            break;

        if (line.startsWith (PluginScanIpc::scanPrefix))
        {
            const auto path = line.fromFirstOccurrenceOf (PluginScanIpc::scanPrefix, false, false);
            if (path.isNotEmpty())
                scanOne (*format, *client, path);
            else
                writeLine (*client, PluginScanIpc::makeFailReply ("empty-path"));
            continue;
        }

        writeLine (*client, PluginScanIpc::makeFailReply ("bad-request"));
    }

    return 0;
}
