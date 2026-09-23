#pragma once

#include <JuceHeader.h>

/** Guard against Windows MIDI drivers that block forever when a device is powered off. */
namespace MidiDeviceAccess
{
    constexpr int defaultTimeoutMs = 1500;

    /** Pull MIDIINPUT entries out of a JUCE device-setup XML so initialise() won't open them. */
    juce::StringArray takeMidiInputIdsFromDeviceSetupXml (juce::XmlElement& xml);

    /** True if MidiInput::openDevice succeeds within timeout (opens then closes). */
    bool probeInput (const juce::String& deviceIdentifier, int timeoutMs = defaultTimeoutMs);

    /** Enable an input on the device manager only after a successful probe. */
    bool enableInput (juce::AudioDeviceManager& deviceManager,
                      const juce::String& deviceIdentifier,
                      int timeoutMs = defaultTimeoutMs);

    /** Open an output on a worker thread; returns nullptr on failure/timeout. */
    std::unique_ptr<juce::MidiOutput> openOutput (const juce::String& deviceIdentifier,
                                                  int timeoutMs = defaultTimeoutMs);
}
