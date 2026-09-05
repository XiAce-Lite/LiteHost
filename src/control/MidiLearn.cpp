#include "MidiLearn.h"

MidiLearnManager::MidiLearnManager() = default;

MidiLearnManager::~MidiLearnManager()
{
    closePort();
}

void MidiLearnManager::setListener (MidiLearnListener* listenerToUse)
{
    listener = listenerToUse;
}

void MidiLearnManager::setDeviceManager (juce::AudioDeviceManager* manager)
{
    if (deviceManager == manager)
        return;

    closePort();
    deviceManager = manager;
    if (enabled)
        openPort();
}

void MidiLearnManager::setEnabled (bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;

    enabled = shouldBeEnabled;
    if (enabled)
        openPort();
    else
        closePort();
}

void MidiLearnManager::setInputDeviceIdentifier (const juce::String& identifier)
{
    if (inputId == identifier)
        return;

    inputId = identifier;
    if (enabled)
    {
        closePort();
        openPort();
    }
}

void MidiLearnManager::closePort()
{
    if (deviceManager != nullptr && inputId.isNotEmpty())
        deviceManager->removeMidiInputDeviceCallback (inputId, this);
}

void MidiLearnManager::openPort()
{
    if (deviceManager == nullptr || ! enabled || inputId.isEmpty())
        return;

    deviceManager->setMidiInputDeviceEnabled (inputId, true);
    deviceManager->addMidiInputDeviceCallback (inputId, this);
}

void MidiLearnManager::applySettings()
{
    closePort();
    if (enabled)
        openPort();
}

void MidiLearnManager::beginLearn (int trackIndex, MidiLearnTarget target)
{
    learning = true;
    learnTrack = trackIndex;
    learnTarget = target;
}

void MidiLearnManager::cancelLearn()
{
    const bool was = learning;
    learning = false;
    if (was && listener != nullptr)
        listener->midiLearnFinished (false);
}

int MidiLearnManager::findBinding (int trackIndex, MidiLearnTarget target) const
{
    for (int i = 0; i < (int) bindings.size(); ++i)
        if (bindings[(size_t) i].trackIndex == trackIndex && bindings[(size_t) i].target == target)
            return i;
    return -1;
}

void MidiLearnManager::clearBinding (int trackIndex, MidiLearnTarget target)
{
    const int index = findBinding (trackIndex, target);
    if (index >= 0)
        bindings.erase (bindings.begin() + index);
}

void MidiLearnManager::clearAll()
{
    bindings.clear();
}

void MidiLearnManager::assignLearning (bool isController, int channel, int number)
{
    if (! learning || listener == nullptr)
        return;

    clearBinding (learnTrack, learnTarget);

    MidiLearnBinding binding;
    binding.trackIndex = learnTrack;
    binding.target = learnTarget;
    binding.isController = isController;
    binding.channel = juce::jlimit (1, 16, channel);
    binding.number = number;
    bindings.push_back (binding);

    learning = false;
    listener->midiLearnFinished (true);
}

void MidiLearnManager::applyBinding (const MidiLearnBinding& binding, const juce::MidiMessage& message)
{
    if (listener == nullptr)
        return;
    if (binding.trackIndex < 0 || binding.trackIndex >= listener->getNumTracks())
        return;

    switch (binding.target)
    {
        case MidiLearnTarget::trim:
        case MidiLearnTarget::gain:
        {
            if (! message.isController())
                return;
            const float norm = (float) message.getControllerValue() / 127.0f;
            if (binding.target == MidiLearnTarget::trim)
            {
                const float db = -24.0f + norm * 48.0f; // -24 .. +24
                listener->setTrackTrim (binding.trackIndex, juce::Decibels::decibelsToGain (db, -24.0f));
            }
            else
            {
                const float db = -60.0f + norm * 72.0f; // -60 .. +12
                listener->setTrackGain (binding.trackIndex, juce::Decibels::decibelsToGain (db, -60.0f));
            }
            break;
        }
        case MidiLearnTarget::pan:
        {
            if (! message.isController())
                return;
            const float pan = ((float) message.getControllerValue() / 127.0f) * 2.0f - 1.0f;
            listener->setTrackPan (binding.trackIndex, pan);
            break;
        }
        case MidiLearnTarget::mute:
        {
            if (! message.isNoteOn() || message.getVelocity() <= 0)
                return;
            listener->setTrackMute (binding.trackIndex, ! listener->getTrackMute (binding.trackIndex));
            break;
        }
        case MidiLearnTarget::solo:
        {
            if (! message.isNoteOn() || message.getVelocity() <= 0)
                return;
            listener->setTrackSolo (binding.trackIndex, ! listener->getTrackSolo (binding.trackIndex));
            break;
        }
    }
}

void MidiLearnManager::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! enabled || listener == nullptr)
        return;

    if (learning)
    {
        if (message.isController())
        {
            assignLearning (true, message.getChannel(), message.getControllerNumber());
            return;
        }
        if (message.isNoteOn() && message.getVelocity() > 0)
        {
            assignLearning (false, message.getChannel(), message.getNoteNumber());
            return;
        }
        return;
    }

    for (const auto& binding : bindings)
    {
        if (message.getChannel() != binding.channel)
            continue;

        if (binding.isController)
        {
            if (message.isController() && message.getControllerNumber() == binding.number)
                applyBinding (binding, message);
        }
        else
        {
            if (message.isNoteOn (false) && message.getNoteNumber() == binding.number)
                applyBinding (binding, message);
        }
    }
}

void MidiLearnManager::writeXml (juce::XmlElement& parent) const
{
    auto* el = parent.createNewChildElement ("MIDI_LEARN");
    el->setAttribute ("enabled", enabled ? 1 : 0);
    el->setAttribute ("midiIn", inputId);

    for (const auto& binding : bindings)
    {
        auto* map = el->createNewChildElement ("MAP");
        map->setAttribute ("track", binding.trackIndex);
        map->setAttribute ("target", midiLearnTargetName (binding.target));
        map->setAttribute ("type", binding.isController ? "cc" : "note");
        map->setAttribute ("ch", binding.channel);
        map->setAttribute ("num", binding.number);
    }
}

void MidiLearnManager::readXml (const juce::XmlElement& parent)
{
    bindings.clear();
    enabled = false;
    inputId.clear();

    if (auto* el = parent.getChildByName ("MIDI_LEARN"))
    {
        enabled = el->getBoolAttribute ("enabled", false);
        inputId = el->getStringAttribute ("midiIn");

        for (auto* map = el->getFirstChildElement(); map != nullptr; map = map->getNextElement())
        {
            if (map->getTagName() != "MAP")
                continue;

            MidiLearnBinding binding;
            binding.trackIndex = map->getIntAttribute ("track", 0);
            binding.target = midiLearnTargetFromName (map->getStringAttribute ("target", "gain"));
            binding.isController = ! map->getStringAttribute ("type", "cc").equalsIgnoreCase ("note");
            binding.channel = map->getIntAttribute ("ch", 1);
            binding.number = map->getIntAttribute ("num", 0);
            bindings.push_back (binding);
        }
    }
}
