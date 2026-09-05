#pragma once

#include <JuceHeader.h>

enum class ControlSurfaceProtocol
{
    mackieControl = 0,
    hui,
    mmc
};

inline juce::String controlSurfaceProtocolName (ControlSurfaceProtocol p)
{
    switch (p)
    {
        case ControlSurfaceProtocol::mackieControl: return "Mackie Control";
        case ControlSurfaceProtocol::hui:           return "HUI";
        case ControlSurfaceProtocol::mmc:           return "MMC";
    }
    return "Mackie Control";
}

inline ControlSurfaceProtocol controlSurfaceProtocolFromName (const juce::String& name)
{
    if (name.equalsIgnoreCase ("HUI"))
        return ControlSurfaceProtocol::hui;
    if (name.equalsIgnoreCase ("MMC"))
        return ControlSurfaceProtocol::mmc;
    return ControlSurfaceProtocol::mackieControl;
}

/** Host-facing actions a control surface can request. */
class ControlSurfaceListener
{
public:
    virtual ~ControlSurfaceListener() = default;

    virtual int getNumTracks() const = 0;
    virtual float getTrackGain (int trackIndex) const = 0;
    virtual float getTrackPan (int trackIndex) const = 0;
    virtual bool getTrackMute (int trackIndex) const = 0;
    virtual bool getTrackSolo (int trackIndex) const = 0;
    virtual float getMasterGain() const = 0;
    virtual bool isAudioEngineRunning() const = 0;

    virtual void setTrackGain (int trackIndex, float gainLinear) = 0;
    virtual void setTrackPan (int trackIndex, float pan) = 0;
    virtual void setTrackMute (int trackIndex, bool mute) = 0;
    virtual void setTrackSolo (int trackIndex, bool solo) = 0;
    virtual void setMasterGain (float gainLinear) = 0;
    virtual void setAudioEngineRunning (bool shouldRun) = 0;
    virtual void controlSurfaceBankChanged (int bankOffset) = 0;
};

class ControlSurfaceManager : private juce::MidiInputCallback
{
public:
    static constexpr int channelsPerBank = 8;

    ControlSurfaceManager();
    ~ControlSurfaceManager() override;

    void setListener (ControlSurfaceListener* listenerToUse);
    void setDeviceManager (juce::AudioDeviceManager* manager);

    void setEnabled (bool shouldBeEnabled);
    bool isEnabled() const noexcept { return enabled; }

    void setProtocol (ControlSurfaceProtocol protocolToUse);
    ControlSurfaceProtocol getProtocol() const noexcept { return protocol; }

    void setInputDeviceIdentifier (const juce::String& identifier);
    void setOutputDeviceIdentifier (const juce::String& identifier);
    juce::String getInputDeviceIdentifier() const { return inputId; }
    juce::String getOutputDeviceIdentifier() const { return outputId; }

    int getBankOffset() const noexcept { return bankOffset; }
    void setBankOffset (int offset);

    void applySettings();
    void refreshFeedback();

    void writeXml (juce::XmlElement& parent) const;
    void readXml (const juce::XmlElement& parent);

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleMackie (const juce::MidiMessage& message);
    void handleHui (const juce::MidiMessage& message);
    void handleMmc (const juce::MidiMessage& message);
    void handleMackieNote (int note, bool isOn);
    void handleMackieFader (int channelZeroBased, int value14);
    void handleMackieVPot (int strip, int ccValue);
    void sendNoteFeedback (int note, bool lit);
    void openPorts();
    void closePorts();
    float fader14ToGain (int value14) const;
    int gainToFader14 (float gainLinear) const;
    int trackIndexForStrip (int strip) const;

    ControlSurfaceListener* listener = nullptr;
    juce::AudioDeviceManager* deviceManager = nullptr;
    std::unique_ptr<juce::MidiOutput> midiOut;
    ControlSurfaceProtocol protocol { ControlSurfaceProtocol::mackieControl };
    juce::String inputId, outputId;
    int bankOffset = 0;
    bool enabled = false;
    bool portsOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSurfaceManager)
};
