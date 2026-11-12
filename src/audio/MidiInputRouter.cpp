#include "MidiInputRouter.h"
#include <cmath>
#include <cstring>

namespace
{
    constexpr int blockBufferReserveBytes = 32 * 1024;
}

MidiInputRouter::MidiInputRouter()
{
    events.resize ((size_t) fifoCapacity);
    allDevices.ensureSize (blockBufferReserveBytes);
    for (auto& buffer : perDevice)
        buffer.ensureSize (blockBufferReserveBytes);
}

void MidiInputRouter::push (const juce::String& deviceId, const juce::MidiMessage& message)
{
    const int size = message.getRawDataSize();
    if (size <= 0 || size > maxEventBytes)
        return;

    double timeStamp = message.getTimeStamp();
    if (timeStamp == 0.0)
        timeStamp = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // Devices may call back on different threads, so producers serialise here.
    const juce::ScopedLock sl (producerLock);

    int slot = routeNone;
    if (deviceId.isNotEmpty())
        for (int s = 0; s < numSlots; ++s)
            if (slotIds[s] == deviceId)
            {
                slot = s;
                break;
            }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 < 1)
        return;

    auto& event = events[(size_t) (size1 > 0 ? start1 : start2)];
    event.timeStamp = timeStamp;
    event.slot = slot;
    event.size = size;
    std::memcpy (event.bytes, message.getRawData(), (size_t) size);
    fifo.finishedWrite (1);
}

int MidiInputRouter::slotForDevice (const juce::String& deviceId, const std::function<bool (int)>& slotInUse)
{
    const juce::ScopedLock sl (producerLock);

    for (int s = 0; s < numSlots; ++s)
        if (slotIds[s] == deviceId)
            return s;

    if (numSlots < maxDeviceSlots)
    {
        slotIds[numSlots] = deviceId;
        return numSlots++;
    }

    for (int s = 0; s < numSlots; ++s)
        if (! slotInUse (s))
        {
            slotIds[s] = deviceId;
            return s;
        }

    return routeNone;
}

void MidiInputRouter::clearBlockBuffers() noexcept
{
    allDevices.clear();
    for (auto& buffer : perDevice)
        buffer.clear();
}

void MidiInputRouter::discardPending() noexcept
{
    fifo.finishedRead (fifo.getNumReady());
}

void MidiInputRouter::drain (int numSamples, double sampleRate) noexcept
{
    clearBlockBuffers();

    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return;

    if (numSamples <= 0)
    {
        fifo.finishedRead (ready);
        return;
    }

    const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double blockSec = sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0;

    auto place = [&] (const Event& event, int ordinal) {
        int pos = 0;
        if (blockSec > 0.0 && event.timeStamp > 0.0)
        {
            const double age = juce::jlimit (0.0, blockSec, nowSec - event.timeStamp);
            pos = juce::jlimit (0, numSamples - 1,
                                (int) std::lround ((blockSec - age) / blockSec * (double) (numSamples - 1)));
        }
        else if (ready > 1)
        {
            pos = (ordinal * (numSamples - 1)) / (ready - 1);
        }

        allDevices.addEvent (event.bytes, event.size, pos);
        if (juce::isPositiveAndBelow (event.slot, maxDeviceSlots))
            perDevice[event.slot].addEvent (event.bytes, event.size, pos);
    };

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (ready, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i)
        place (events[(size_t) (start1 + i)], i);
    for (int i = 0; i < size2; ++i)
        place (events[(size_t) (start2 + i)], size1 + i);
    fifo.finishedRead (size1 + size2);
}

const juce::MidiBuffer& MidiInputRouter::bufferForRoute (int route) const noexcept
{
    if (route == routeAll)
        return allDevices;
    if (juce::isPositiveAndBelow (route, maxDeviceSlots))
        return perDevice[route];
    return empty;
}
