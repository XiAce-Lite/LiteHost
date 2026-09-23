#include "MidiDeviceAccess.h"
#include "CrashLog.h"
#include <atomic>
#include <thread>

namespace
{
    struct NullMidiCallback final : public juce::MidiInputCallback
    {
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override {}
    };

    template <typename Pred>
    bool waitWithUiPump (Pred&& isDone, int timeoutMs)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (1, timeoutMs);
        while (! isDone() && juce::Time::getMillisecondCounter() < deadline)
        {
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            {
                if (mm->isThisTheMessageThread())
                    mm->runDispatchLoopUntil (20);
                else
                    juce::Thread::sleep (20);
            }
            else
            {
                juce::Thread::sleep (20);
            }
        }
        return isDone();
    }
}

juce::StringArray MidiDeviceAccess::takeMidiInputIdsFromDeviceSetupXml (juce::XmlElement& xml)
{
    juce::StringArray ids;

    for (auto* e = xml.getFirstChildElement(); e != nullptr;)
    {
        auto* next = e->getNextElement();
        if (e->hasTagName ("MIDIINPUT"))
        {
            auto id = e->getStringAttribute ("identifier");
            if (id.isEmpty())
                id = e->getStringAttribute ("name");
            if (id.isNotEmpty())
                ids.addIfNotAlreadyThere (id);
            xml.removeChildElement (e, true);
        }
        e = next;
    }

    return ids;
}

bool MidiDeviceAccess::probeInput (const juce::String& deviceIdentifier, int timeoutMs)
{
    if (deviceIdentifier.isEmpty())
        return false;

    std::atomic<int> result { 0 }; // 0 pending, 1 ok, 2 fail
    std::thread worker ([&deviceIdentifier, &result] {
        NullMidiCallback cb;
        if (auto input = juce::MidiInput::openDevice (deviceIdentifier, &cb))
        {
            input.reset();
            result.store (1, std::memory_order_release);
        }
        else
        {
            result.store (2, std::memory_order_release);
        }
    });

    const bool finished = waitWithUiPump ([&result] {
        return result.load (std::memory_order_acquire) != 0;
    }, timeoutMs);

    if (! finished)
    {
        worker.detach();
        CrashLog::write ("MIDI input probe timed out: " + deviceIdentifier);
        return false;
    }

    if (worker.joinable())
        worker.join();

    return result.load (std::memory_order_acquire) == 1;
}

bool MidiDeviceAccess::enableInput (juce::AudioDeviceManager& deviceManager,
                                    const juce::String& deviceIdentifier,
                                    int timeoutMs)
{
    if (deviceIdentifier.isEmpty())
        return false;

    if (deviceManager.isMidiInputDeviceEnabled (deviceIdentifier))
        return true;

    if (! probeInput (deviceIdentifier, timeoutMs))
        return false;

    deviceManager.setMidiInputDeviceEnabled (deviceIdentifier, true);
    const bool ok = deviceManager.isMidiInputDeviceEnabled (deviceIdentifier);
    if (! ok)
        CrashLog::write ("MIDI input enable failed: " + deviceIdentifier);
    return ok;
}

std::unique_ptr<juce::MidiOutput> MidiDeviceAccess::openOutput (const juce::String& deviceIdentifier,
                                                                int timeoutMs)
{
    if (deviceIdentifier.isEmpty())
        return {};

    std::atomic<juce::MidiOutput*> raw { nullptr };
    std::atomic<bool> done { false };

    std::thread worker ([&deviceIdentifier, &raw, &done] {
        raw.store (juce::MidiOutput::openDevice (deviceIdentifier).release(), std::memory_order_release);
        done.store (true, std::memory_order_release);
    });

    if (! waitWithUiPump ([&done] { return done.load (std::memory_order_acquire); }, timeoutMs))
    {
        worker.detach();
        CrashLog::write ("MIDI output open timed out: " + deviceIdentifier);
        return {};
    }

    if (worker.joinable())
        worker.join();

    return std::unique_ptr<juce::MidiOutput> (raw.load (std::memory_order_acquire));
}
