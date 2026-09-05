#pragma once

#include <JuceHeader.h>
#include <vector>

enum class MidiLearnTarget
{
    trim = 0,
    gain,
    pan,
    mute,
    solo
};

inline juce::String midiLearnTargetName (MidiLearnTarget t)
{
    switch (t)
    {
        case MidiLearnTarget::trim: return "trim";
        case MidiLearnTarget::gain: return "gain";
        case MidiLearnTarget::pan:  return "pan";
        case MidiLearnTarget::mute: return "mute";
        case MidiLearnTarget::solo: return "solo";
    }
    return "gain";
}

inline MidiLearnTarget midiLearnTargetFromName (const juce::String& name)
{
    if (name.equalsIgnoreCase ("trim")) return MidiLearnTarget::trim;
    if (name.equalsIgnoreCase ("pan"))  return MidiLearnTarget::pan;
    if (name.equalsIgnoreCase ("mute")) return MidiLearnTarget::mute;
    if (name.equalsIgnoreCase ("solo")) return MidiLearnTarget::solo;
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
