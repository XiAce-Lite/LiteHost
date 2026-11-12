#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

/** Moves incoming MIDI from device threads to the audio thread without locks on the audio side,
    and splits each block's events per device.

    Routes: routeNone (no MIDI), routeAll (every device), or a device slot in [0, maxDeviceSlots). */
class MidiInputRouter
{
public:
    static constexpr int routeNone = -1;
    static constexpr int routeAll = -2;
    static constexpr int maxDeviceSlots = 16;
    /** Longer messages (large SysEx) are dropped. */
    static constexpr int maxEventBytes = 256;

    MidiInputRouter();

    /** MIDI input threads. Drops the event when the queue is full. */
    void push (const juce::String& deviceId, const juce::MidiMessage& message);

    /** Message thread. Slot for deviceId, registering it if needed. When the table is full, recycles a slot
        for which slotInUse returns false; returns routeNone if none can be freed. */
    int slotForDevice (const juce::String& deviceId, const std::function<bool (int)>& slotInUse);

    /** Audio thread: move queued events into this block's buffers. */
    void drain (int numSamples, double sampleRate) noexcept;
    /** Audio thread (or while the device is stopped). */
    void discardPending() noexcept;
    void clearBlockBuffers() noexcept;
    const juce::MidiBuffer& bufferForRoute (int route) const noexcept;

private:
    static constexpr int fifoCapacity = 2048;

    struct Event
    {
        double timeStamp = 0.0;
        int slot = routeNone;
        int size = 0;
        juce::uint8 bytes[maxEventBytes] {};
    };

    /** Device threads + message thread only; the audio thread never takes it. */
    juce::CriticalSection producerLock;
    juce::AbstractFifo fifo { fifoCapacity };
    std::vector<Event> events;
    juce::String slotIds[maxDeviceSlots];
    int numSlots = 0;

    juce::MidiBuffer allDevices;
    juce::MidiBuffer perDevice[maxDeviceSlots];
    const juce::MidiBuffer empty;
};
