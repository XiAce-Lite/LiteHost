#include "ControlSurface.h"
#include <cmath>

namespace
{
    // Mackie Control Universal note numbers (common MCU chart).
    constexpr int kMuteBase = 0x10;
    constexpr int kSoloBase = 0x08;
    constexpr int kSelectBase = 0x18;
    constexpr int kRecBase = 0x00;
    constexpr int kVPotSwitchBase = 0x20;
    constexpr int kVPotCCBase = 0x10; // CC 16-23
    constexpr int kBankLeft = 0x2e;
    constexpr int kBankRight = 0x2f;
    constexpr int kChannelLeft = 0x30;
    constexpr int kChannelRight = 0x31;
    constexpr int kRewind = 0x5b;
    constexpr int kFastForward = 0x5c;
    constexpr int kStop = 0x5d;
    constexpr int kPlay = 0x5e;
    constexpr int kRecord = 0x5f;
}

ControlSurfaceManager::ControlSurfaceManager() = default;

ControlSurfaceManager::~ControlSurfaceManager()
{
    closePorts();
}

void ControlSurfaceManager::setListener (ControlSurfaceListener* listenerToUse)
{
    listener = listenerToUse;
}

void ControlSurfaceManager::setDeviceManager (juce::AudioDeviceManager* manager)
{
    if (deviceManager == manager)
        return;

    closePorts();
    deviceManager = manager;
    if (enabled)
        openPorts();
}

void ControlSurfaceManager::setEnabled (bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;

    enabled = shouldBeEnabled;
    if (enabled)
        openPorts();
    else
        closePorts();
}

void ControlSurfaceManager::setProtocol (ControlSurfaceProtocol protocolToUse)
{
    protocol = protocolToUse;
}

void ControlSurfaceManager::setInputDeviceIdentifier (const juce::String& identifier)
{
    if (inputId == identifier)
        return;
    inputId = identifier;
    if (enabled)
    {
        closePorts();
        openPorts();
    }
}

void ControlSurfaceManager::setOutputDeviceIdentifier (const juce::String& identifier)
{
    if (outputId == identifier)
        return;
    outputId = identifier;
    if (enabled)
    {
        closePorts();
        openPorts();
    }
}

void ControlSurfaceManager::setBankOffset (int offset)
{
    bankOffset = juce::jmax (0, offset);
    if (listener != nullptr)
        listener->controlSurfaceBankChanged (bankOffset);
    refreshFeedback();
}

void ControlSurfaceManager::applySettings()
{
    if (enabled)
    {
        closePorts();
        openPorts();
    }
    else
    {
        closePorts();
    }
    refreshFeedback();
}

void ControlSurfaceManager::closePorts()
{
    if (deviceManager != nullptr && inputId.isNotEmpty())
        deviceManager->removeMidiInputDeviceCallback (inputId, this);

    midiOut.reset();
    portsOpen = false;
}

void ControlSurfaceManager::openPorts()
{
    if (deviceManager == nullptr || ! enabled)
        return;

    if (inputId.isNotEmpty())
    {
        deviceManager->setMidiInputDeviceEnabled (inputId, true);
        deviceManager->addMidiInputDeviceCallback (inputId, this);
    }

    if (outputId.isNotEmpty())
        midiOut = juce::MidiOutput::openDevice (outputId);

    portsOpen = inputId.isNotEmpty();
}

int ControlSurfaceManager::trackIndexForStrip (int strip) const
{
    return bankOffset + strip;
}

float ControlSurfaceManager::fader14ToGain (int value14) const
{
    const float norm = juce::jlimit (0.0f, 1.0f, (float) value14 / 16383.0f);
    // Map to -60 .. +12 dB (same range as track faders).
    const float db = -60.0f + norm * 72.0f;
    return juce::Decibels::decibelsToGain (db, -60.0f);
}

int ControlSurfaceManager::gainToFader14 (float gainLinear) const
{
    const float db = juce::Decibels::gainToDecibels (gainLinear, -60.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 72.0f);
    return juce::jlimit (0, 16383, (int) std::lround (norm * 16383.0f));
}

void ControlSurfaceManager::sendNoteFeedback (int note, bool lit)
{
    if (midiOut == nullptr)
        return;
    midiOut->sendMessageNow (juce::MidiMessage::noteOn (1, note, lit ? (juce::uint8) 127 : (juce::uint8) 0));
}

void ControlSurfaceManager::refreshFeedback()
{
    if (listener == nullptr || midiOut == nullptr || ! enabled)
        return;

    if (protocol != ControlSurfaceProtocol::mackieControl)
        return;

    for (int strip = 0; strip < channelsPerBank; ++strip)
    {
        const int track = trackIndexForStrip (strip);
        const bool hasTrack = track < listener->getNumTracks();
        sendNoteFeedback (kMuteBase + strip, hasTrack && listener->getTrackMute (track));
        sendNoteFeedback (kSoloBase + strip, hasTrack && listener->getTrackSolo (track));

        const int faderValue = hasTrack ? gainToFader14 (listener->getTrackGain (track)) : 0;
        midiOut->sendMessageNow (juce::MidiMessage::pitchWheel (strip + 1, faderValue));
    }

    midiOut->sendMessageNow (juce::MidiMessage::pitchWheel (9, gainToFader14 (listener->getMasterGain())));
    sendNoteFeedback (kPlay, listener->isAudioEngineRunning());
    sendNoteFeedback (kStop, ! listener->isAudioEngineRunning());
}

void ControlSurfaceManager::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! enabled || listener == nullptr)
        return;

    switch (protocol)
    {
        case ControlSurfaceProtocol::mackieControl: handleMackie (message); break;
        case ControlSurfaceProtocol::hui:           handleHui (message); break;
        case ControlSurfaceProtocol::mmc:           handleMmc (message); break;
    }
}

void ControlSurfaceManager::handleMackieVPot (int strip, int ccValue)
{
    if (listener == nullptr || ! juce::isPositiveAndBelow (strip, channelsPerBank))
        return;

    const int track = trackIndexForStrip (strip);
    if (track >= listener->getNumTracks())
        return;

    // MCU relative: bits 0-5 = steps, bit 6 = direction (1 = decrease).
    int delta = ccValue & 0x3f;
    if ((ccValue & 0x40) != 0)
        delta = -delta;

    if (delta == 0)
        return;

    const float next = juce::jlimit (-1.0f, 1.0f, listener->getTrackPan (track) + (float) delta * 0.03f);
    listener->setTrackPan (track, next);
}

void ControlSurfaceManager::handleMackieFader (int channelZeroBased, int value14)
{
    if (listener == nullptr)
        return;

    const float gain = fader14ToGain (value14);
    if (channelZeroBased >= 0 && channelZeroBased < channelsPerBank)
    {
        const int track = trackIndexForStrip (channelZeroBased);
        if (track < listener->getNumTracks())
            listener->setTrackGain (track, gain);
    }
    else if (channelZeroBased == 8) // master
    {
        listener->setMasterGain (gain);
    }
}

void ControlSurfaceManager::handleMackieNote (int note, bool isOn)
{
    if (listener == nullptr || ! isOn)
        return;

    if (note >= kVPotSwitchBase && note < kVPotSwitchBase + channelsPerBank)
    {
        const int strip = note - kVPotSwitchBase;
        const int track = trackIndexForStrip (strip);
        if (track < listener->getNumTracks())
            listener->setTrackPan (track, 0.0f);
        return;
    }

    if (note >= kMuteBase && note < kMuteBase + channelsPerBank)
    {
        const int strip = note - kMuteBase;
        const int track = trackIndexForStrip (strip);
        if (track < listener->getNumTracks())
        {
            const bool next = ! listener->getTrackMute (track);
            listener->setTrackMute (track, next);
            sendNoteFeedback (note, next);
        }
        return;
    }

    if (note >= kSoloBase && note < kSoloBase + channelsPerBank)
    {
        const int strip = note - kSoloBase;
        const int track = trackIndexForStrip (strip);
        if (track < listener->getNumTracks())
        {
            const bool next = ! listener->getTrackSolo (track);
            listener->setTrackSolo (track, next);
            sendNoteFeedback (note, next);
        }
        return;
    }

    if (note == kBankLeft || note == kChannelLeft)
    {
        setBankOffset (juce::jmax (0, bankOffset - channelsPerBank));
        return;
    }

    if (note == kBankRight || note == kChannelRight)
    {
        if (listener->getNumTracks() > 0)
        {
            const int maxBank = juce::jmax (0, ((listener->getNumTracks() + channelsPerBank - 1) / channelsPerBank - 1)
                                                  * channelsPerBank);
            setBankOffset (juce::jmin (bankOffset + channelsPerBank, maxBank));
        }
        return;
    }

    if (note == kPlay)
    {
        listener->setAudioEngineRunning (true);
        refreshFeedback();
        return;
    }

    if (note == kStop || note == kRewind)
    {
        listener->setAudioEngineRunning (false);
        refreshFeedback();
        return;
    }

    juce::ignoreUnused (kSelectBase, kRecBase, kFastForward, kRecord);
}

void ControlSurfaceManager::handleMackie (const juce::MidiMessage& message)
{
    if (message.isPitchWheel())
    {
        handleMackieFader (message.getChannel() - 1, message.getPitchWheelValue());
        return;
    }

    if (message.isController())
    {
        const int cc = message.getControllerNumber();
        if (cc >= kVPotCCBase && cc < kVPotCCBase + channelsPerBank)
        {
            handleMackieVPot (cc - kVPotCCBase, message.getControllerValue());
            return;
        }
    }

    if (message.isNoteOn (false) || message.isNoteOff())
    {
        const bool on = message.isNoteOn() && message.getVelocity() > 0;
        handleMackieNote (message.getNoteNumber(), on);
    }
}

void ControlSurfaceManager::handleHui (const juce::MidiMessage& message)
{
    // HUI uses a different zone/port encoding. Map common fader-like pitch bends
    // and a few notes similarly for basic compatibility; full HUI can grow later.
    if (message.isPitchWheel())
    {
        handleMackieFader (message.getChannel() - 1, message.getPitchWheelValue());
        return;
    }

    if (message.isController() && message.getControllerNumber() == 0x0c)
    {
        // HUI often uses CC for zone selection; ignore for now.
        return;
    }

    if (message.isNoteOn (false) || message.isNoteOff())
    {
        // Reuse MCU-style note numbers when surfaces emit them in HUI-compat modes.
        const bool on = message.isNoteOn() && message.getVelocity() > 0;
        handleMackieNote (message.getNoteNumber(), on);
    }
}

void ControlSurfaceManager::handleMmc (const juce::MidiMessage& message)
{
    if (! message.isSysEx() || listener == nullptr)
        return;

    const auto* data = message.getSysExData();
    const int size = message.getSysExDataSize();
    // F0 7F <device> 06 <command> ... F7  → getSysExData excludes F0/F7
    if (size < 4 || data[0] != 0x7f || data[2] != 0x06)
        return;

    const auto command = data[3];
    switch (command)
    {
        case 0x01: // stop
        case 0x09: // pause
            listener->setAudioEngineRunning (false);
            break;
        case 0x02: // play
        case 0x03: // deferred play
            listener->setAudioEngineRunning (true);
            break;
        default:
            break;
    }
}

void ControlSurfaceManager::writeXml (juce::XmlElement& parent) const
{
    auto* el = parent.createNewChildElement ("SURFACE");
    el->setAttribute ("enabled", enabled ? 1 : 0);
    el->setAttribute ("protocol", controlSurfaceProtocolName (protocol));
    el->setAttribute ("midiIn", inputId);
    el->setAttribute ("midiOut", outputId);
    el->setAttribute ("bank", bankOffset);
}

void ControlSurfaceManager::readXml (const juce::XmlElement& parent)
{
    if (auto* el = parent.getChildByName ("SURFACE"))
    {
        protocol = controlSurfaceProtocolFromName (el->getStringAttribute ("protocol", "Mackie Control"));
        inputId = el->getStringAttribute ("midiIn");
        outputId = el->getStringAttribute ("midiOut");
        bankOffset = el->getIntAttribute ("bank", 0);
        enabled = el->getBoolAttribute ("enabled", false);
    }
}
