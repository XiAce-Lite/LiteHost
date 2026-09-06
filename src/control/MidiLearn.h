#pragma once

#include <JuceHeader.h>
#include <vector>

enum class MidiLearnTarget
{
    trim = 0,
    gain,
    pan,
    mute,
    solo,
    masterGain,
    reverbEnabled,
    reverbMix,
    reverbSize,
    limiterEnabled,
    limiterCeiling,
    gateEnabled,
    gateThreshold
};

inline constexpr int midiLearnMasterTrack = -1;

inline bool midiLearnTargetIsMaster (MidiLearnTarget t)
{
    switch (t)
    {
        case MidiLearnTarget::masterGain:
        case MidiLearnTarget::reverbEnabled:
        case MidiLearnTarget::reverbMix:
        case MidiLearnTarget::reverbSize:
        case MidiLearnTarget::limiterEnabled:
        case MidiLearnTarget::limiterCeiling:
        case MidiLearnTarget::gateEnabled:
        case MidiLearnTarget::gateThreshold:
            return true;
        default:
            return false;
    }
}

inline juce::String midiLearnTargetName (MidiLearnTarget t)
{
    switch (t)
    {
        case MidiLearnTarget::trim:            return "trim";
        case MidiLearnTarget::gain:            return "gain";
        case MidiLearnTarget::pan:             return "pan";
        case MidiLearnTarget::mute:            return "mute";
        case MidiLearnTarget::solo:            return "solo";
        case MidiLearnTarget::masterGain:      return "masterGain";
        case MidiLearnTarget::reverbEnabled:   return "reverbEnabled";
        case MidiLearnTarget::reverbMix:       return "reverbMix";
        case MidiLearnTarget::reverbSize:      return "reverbSize";
        case MidiLearnTarget::limiterEnabled:  return "limiterEnabled";
        case MidiLearnTarget::limiterCeiling:  return "limiterCeiling";
        case MidiLearnTarget::gateEnabled:     return "gateEnabled";
        case MidiLearnTarget::gateThreshold:   return "gateThreshold";
    }
    return "gain";
}

inline MidiLearnTarget midiLearnTargetFromName (const juce::String& name)
{
    if (name.equalsIgnoreCase ("trim"))           return MidiLearnTarget::trim;
    if (name.equalsIgnoreCase ("pan"))            return MidiLearnTarget::pan;
    if (name.equalsIgnoreCase ("mute"))           return MidiLearnTarget::mute;
    if (name.equalsIgnoreCase ("solo"))           return MidiLearnTarget::solo;
    if (name.equalsIgnoreCase ("masterGain"))     return MidiLearnTarget::masterGain;
    if (name.equalsIgnoreCase ("reverbEnabled"))  return MidiLearnTarget::reverbEnabled;
    if (name.equalsIgnoreCase ("reverbMix"))      return MidiLearnTarget::reverbMix;
    if (name.equalsIgnoreCase ("reverbSize"))     return MidiLearnTarget::reverbSize;
    if (name.equalsIgnoreCase ("limiterEnabled")) return MidiLearnTarget::limiterEnabled;
    if (name.equalsIgnoreCase ("limiterCeiling")) return MidiLearnTarget::limiterCeiling;
    if (name.equalsIgnoreCase ("gateEnabled"))    return MidiLearnTarget::gateEnabled;
    if (name.equalsIgnoreCase ("gateThreshold"))  return MidiLearnTarget::gateThreshold;
    return MidiLearnTarget::gain;
}

class MidiLearnListener
{
public:
    virtual ~MidiLearnListener() = default;

    virtual int getNumTracks() const = 0;
    virtual void setTrackTrim (int trackIndex, float gainLinear) = 0;
    virtual void setTrackGain (int trackIndex, float gainLinear) = 0;
    virtual void setTrackPan (int trackIndex, float pan) = 0;
    virtual void setTrackMute (int trackIndex, bool mute) = 0;
    virtual void setTrackSolo (int trackIndex, bool solo) = 0;
    virtual bool getTrackMute (int trackIndex) const = 0;
    virtual bool getTrackSolo (int trackIndex) const = 0;

    virtual void setMasterGain (float gainLinear) = 0;
    virtual bool getReverbEnabled() const = 0;
    virtual void setReverbEnabled (bool enabled) = 0;
    virtual void setReverbMix (float wet) = 0;
    virtual void setReverbSize (float size) = 0;
    virtual bool getLimiterEnabled() const = 0;
    virtual void setLimiterEnabled (bool enabled) = 0;
    virtual void setLimiterCeilingDb (float db) = 0;
    virtual bool getGateEnabled() const = 0;
    virtual void setGateEnabled (bool enabled) = 0;
    virtual void setGateThresholdDb (float db) = 0;

    virtual void midiLearnFinished (bool assigned) = 0;
};

struct MidiLearnBinding
{
    int trackIndex = 0;
    MidiLearnTarget target { MidiLearnTarget::gain };
    bool isController = true; // false = note
    int channel = 1;          // 1-16
    int number = 0;           // CC or note
};

class MidiLearnManager : private juce::MidiInputCallback
{
public:
    MidiLearnManager();
    ~MidiLearnManager() override;

    void setListener (MidiLearnListener* listenerToUse);
    void setDeviceManager (juce::AudioDeviceManager* manager);

    void setEnabled (bool shouldBeEnabled);
    bool isEnabled() const noexcept { return enabled; }

    void setInputDeviceIdentifier (const juce::String& identifier);
    juce::String getInputDeviceIdentifier() const { return inputId; }

    void beginLearn (int trackIndex, MidiLearnTarget target);
    void cancelLearn();
    bool isLearning() const noexcept { return learning; }
    int getLearningTrack() const noexcept { return learnTrack; }
    MidiLearnTarget getLearningTarget() const noexcept { return learnTarget; }

    void clearBinding (int trackIndex, MidiLearnTarget target);
    void clearAll();
    void trackMoved (int fromIndex, int destIndex);

    void applySettings();
    void writeXml (juce::XmlElement& parent) const;
    void readXml (const juce::XmlElement& parent);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override;
    void openPort();
    void closePort();
    void assignLearning (bool isController, int channel, int number);
    void applyBinding (const MidiLearnBinding& binding, const juce::MidiMessage& message);
    int findBinding (int trackIndex, MidiLearnTarget target) const;

    MidiLearnListener* listener = nullptr;
    juce::AudioDeviceManager* deviceManager = nullptr;
    juce::String inputId;
    std::vector<MidiLearnBinding> bindings;
    bool enabled = false;
    bool learning = false;
    int learnTrack = 0;
    MidiLearnTarget learnTarget { MidiLearnTarget::gain };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnManager)
};
