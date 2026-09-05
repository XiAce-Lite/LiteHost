#include "MixerStrips.h"
#include "MainComponent.h"
#include "LookAndFeel.h"
#include "Utf8.h"

namespace
{
    int inputComboId (int start, int count)
    {
        if (start < 0)
            return 1;

        return (start + 2) * 10 + juce::jlimit (1, 2, count);
    }

    void decodeInputComboId (int id, int& start, int& count)
    {
        if (id <= 1)
        {
            start = -1;
            count = 2;
            return;
        }

        count = id % 10;
        start = id / 10 - 2;
    }

    constexpr int midiComboIdBase = 10000;
    constexpr int midiAllComboId = 9999;

    int midiComboId (int deviceIndex)
    {
        return midiComboIdBase + deviceIndex;
    }

    bool isMidiComboId (int id)
    {
        return id == midiAllComboId || id >= midiComboIdBase;
    }
}

TrackStrip::TrackStrip (MainComponent& ownerIn, TrackProcessor& trackIn)
    : owner (ownerIn),
      track (trackIn)
{
    name.setText (track.name, juce::dontSendNotification);
    name.setFont (LiteLookAndFeel::uiFont (14.0f));
    name.setJustificationType (juce::Justification::centred);
    name.setEditable (true, true, false);
    name.setColour (juce::Label::backgroundColourId, juce::Colour (LiteLookAndFeel::raised));
    name.setColour (juce::Label::outlineColourId, juce::Colour (0xff3a4254));
    name.setColour (juce::Label::textWhenEditingColourId, juce::Colour (LiteLookAndFeel::text));
    name.onTextChange = [this] {
        auto text = name.getText().trim();
        if (text.isEmpty())
        {
            name.setText (track.name, juce::dontSendNotification);
            return;
        }
        track.name = text;
    };
    addAndMakeVisible (name);

    input.setTextWhenNoChoicesAvailable (jp (u8"入力なし"));
    input.onChange = [this] {
        const int id = input.getSelectedId();
        if (id == midiComboIdBase - 1)
            return;

        if (isMidiComboId (id))
        {
            juce::String midiId;
            if (id == midiAllComboId)
            {
                midiId = AudioEngine::midiAllDevicesId;
            }
            else
            {
                const auto devices = juce::MidiInput::getAvailableDevices();
                const int index = id - midiComboIdBase;
                if (juce::isPositiveAndBelow (index, devices.size()))
                    midiId = devices[index].identifier;
            }

            {
                const juce::ScopedLock sl (owner.getEngine().getCallbackLock());
                track.inputStart = -1;
                track.inputCount = 2;
                track.midiDeviceId = midiId;
            }
            owner.syncTrackMidiInputs();
            return;
        }

        int start = -1, count = 2;
        decodeInputComboId (id, start, count);
        {
            const juce::ScopedLock sl (owner.getEngine().getCallbackLock());
            track.inputStart = start;
            track.inputCount = count;
            track.midiDeviceId.clear();
        }
    };
    addAndMakeVisible (input);

    mute.setClickingTogglesState (true);
    mute.setButtonText ("M");
    mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LiteLookAndFeel::danger));
    mute.setToggleState (track.mute.load(), juce::dontSendNotification);
    mute.onClick = [this] { track.mute = mute.getToggleState(); };
    mute.addMouseListener (&learnClicks, false);
    addAndMakeVisible (mute);

    solo.setClickingTogglesState (true);
    solo.setButtonText ("S");
    solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LiteLookAndFeel::solo));
    solo.setToggleState (track.solo.load(), juce::dontSendNotification);
    solo.onClick = [this] { track.solo = solo.getToggleState(); };
    solo.addMouseListener (&learnClicks, false);
    addAndMakeVisible (solo);

    gain.setSliderStyle (juce::Slider::LinearVertical);
    gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    gain.setRange (-60.0, 12.0, 0.1);
    gain.setValue ((double) juce::Decibels::gainToDecibels (track.gain.load(), -60.0f), juce::dontSendNotification);
    gain.setTextValueSuffix (" dB");
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.onValueChange = [this] {
        track.gain = juce::Decibels::decibelsToGain ((float) gain.getValue(), -60.0f);
    };
    gain.addMouseListener (&learnClicks, false);
    addAndMakeVisible (gain);

    trimLabel.setText ("Trim", juce::dontSendNotification);
    trimLabel.setJustificationType (juce::Justification::centred);
    trimLabel.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (trimLabel);

    trim.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    trim.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    trim.setRange (-24.0, 24.0, 0.1);
    trim.setValue ((double) juce::Decibels::gainToDecibels (track.trim.load(), -24.0f), juce::dontSendNotification);
    trim.setTextValueSuffix (" dB");
    trim.setDoubleClickReturnValue (true, 0.0);
    trim.onValueChange = [this] {
        track.trim = juce::Decibels::decibelsToGain ((float) trim.getValue(), -24.0f);
    };
    trim.addMouseListener (&learnClicks, false);
    addAndMakeVisible (trim);

    panLabel.setText ("Pan", juce::dontSendNotification);
    panLabel.setJustificationType (juce::Justification::centred);
    panLabel.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (panLabel);

    pan.setSliderStyle (juce::Slider::LinearHorizontal);
    pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setValue ((double) track.pan.load(), juce::dontSendNotification);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.textFromValueFunction = [] (double v) {
        if (std::abs (v) < 0.01)
            return juce::String ("C");
        if (v < 0.0)
            return "L" + juce::String ((int) std::lround (-v * 100.0));
        return "R" + juce::String ((int) std::lround (v * 100.0));
    };
    pan.valueFromTextFunction = [] (const juce::String& t) {
        const auto s = t.trim().toUpperCase();
        if (s == "C" || s == "0")
            return 0.0;
        if (s.startsWithChar ('L'))
            return -juce::jlimit (0.0, 1.0, s.substring (1).getDoubleValue() / 100.0);
        if (s.startsWithChar ('R'))
            return juce::jlimit (0.0, 1.0, s.substring (1).getDoubleValue() / 100.0);
        return juce::jlimit (-1.0, 1.0, s.getDoubleValue());
    };
    pan.onValueChange = [this] { track.pan = (float) pan.getValue(); };
    pan.addMouseListener (&learnClicks, false);
    addAndMakeVisible (pan);

    addFx.setButtonText ("+ VST");
    addFx.setTooltip (jp (u8"トラックに VST3 を追加（最大 10）"));
    addFx.onClick = [this] { owner.promptAddPlugin (track.id, false); };
    addAndMakeVisible (addFx);

    remove.setButtonText (jp (u8"削除"));
    remove.onClick = [this] { owner.removeTrack (track.id); };
    addAndMakeVisible (remove);

    chips.onOpen = [this] (int index) {
        if (auto* plugin = track.plugins.get (index))
            owner.openPluginEditor (*plugin);
    };
    chips.onRemove = [this] (int index) { owner.removePluginFromTrack (track.id, index); };
    chips.onBypassChanged = [this] (int index, bool bypassed) {
        track.plugins.setBypassed (index, bypassed);
    };
    addAndMakeVisible (chips);
    addAndMakeVisible (meter);

    refreshInputs();
    refreshPlugins();
}

void TrackStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
    g.setColour (juce::Colour (LiteLookAndFeel::surface));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xff2c3344));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void TrackStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 10);
    name.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    input.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);

    auto ms = r.removeFromTop (28);
    mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
    solo.setBounds (ms.reduced (2, 0));
    r.removeFromTop (4);

    auto trimRow = r.removeFromTop (72);
    trimLabel.setBounds (trimRow.removeFromTop (14));
    trim.setBounds (trimRow);

    panLabel.setBounds (r.removeFromTop (14));
    pan.setBounds (r.removeFromTop (34));
    r.removeFromTop (4);

    auto meterRow = r.removeFromTop (100);
    meter.setBounds (meterRow.removeFromRight (14).reduced (0, 4));
    gain.setBounds (meterRow);

    r.removeFromTop (6);
    remove.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (4);
    addFx.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (6);
    chips.setBounds (r);
}

void TrackStrip::refreshInputs()
{
    input.clear (juce::dontSendNotification);
    input.addItem (jp (u8"(入力なし)"), 1);

    if (auto* device = owner.getDeviceManager().getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();

        for (int i = 0; i < names.size(); ++i)
        {
            if (! active[i])
                continue;

            input.addItem (names[i], inputComboId (i, 1));

            if (i + 1 < names.size() && active[i + 1])
                input.addItem (names[i] + " / " + names[i + 1], inputComboId (i, 2));
        }
    }

    const auto midiDevices = juce::MidiInput::getAvailableDevices();
    input.addSeparator();
    input.addItem (jp (u8"MIDI: すべて"), midiAllComboId);

    for (int i = 0; i < midiDevices.size(); ++i)
        input.addItem ("MIDI: " + midiDevices[i].name, midiComboId (i));

    if (track.midiDeviceId == AudioEngine::midiAllDevicesId)
    {
        input.setSelectedId (midiAllComboId, juce::dontSendNotification);
    }
    else if (track.midiDeviceId.isNotEmpty())
    {
        bool found = false;
        for (int i = 0; i < midiDevices.size(); ++i)
        {
            if (midiDevices[i].identifier == track.midiDeviceId)
            {
                input.setSelectedId (midiComboId (i), juce::dontSendNotification);
                found = true;
                break;
            }
        }

        if (! found)
        {
            input.addItem ("MIDI: (" + track.midiDeviceId + ")", midiComboIdBase - 1);
            input.setSelectedId (midiComboIdBase - 1, juce::dontSendNotification);
        }
    }
    else
    {
        input.setSelectedId (inputComboId (track.inputStart.load(), track.inputCount.load()),
                             juce::dontSendNotification);
        if (input.getSelectedId() == 0)
            input.setSelectedId (1, juce::dontSendNotification);
    }
}

void TrackStrip::refreshPlugins()
{
    juce::StringArray names;
    juce::Array<bool> active;
    for (int i = 0; i < track.plugins.size(); ++i)
    {
        if (auto* plugin = track.plugins.get (i))
            names.add (plugin->getName());
        active.add (! track.plugins.isBypassed (i));
    }
    chips.setPlugins (names, active);
    addFx.setEnabled (track.plugins.canAdd());
}

void TrackStrip::setPeak (float value)
{
    meter.setLevel (value);
    meter.tick (50.0f);
}

juce::Uuid TrackStrip::getTrackId() const
{
    return track.id;
}

void TrackStrip::syncFromTrack()
{
    mute.setToggleState (track.mute.load(), juce::dontSendNotification);
    solo.setToggleState (track.solo.load(), juce::dontSendNotification);
    gain.setValue ((double) juce::Decibels::gainToDecibels (track.gain.load(), -60.0f),
                   juce::dontSendNotification);
    trim.setValue ((double) juce::Decibels::gainToDecibels (track.trim.load(), -24.0f),
                   juce::dontSendNotification);
    pan.setValue ((double) track.pan.load(), juce::dontSendNotification);
    refreshInputs();
}

void TrackStrip::LearnClickListener::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    MidiLearnTarget target = MidiLearnTarget::gain;
    if (e.eventComponent == &strip.trim)
        target = MidiLearnTarget::trim;
    else if (e.eventComponent == &strip.pan)
        target = MidiLearnTarget::pan;
    else if (e.eventComponent == &strip.mute)
        target = MidiLearnTarget::mute;
    else if (e.eventComponent == &strip.solo)
        target = MidiLearnTarget::solo;
    else if (e.eventComponent == &strip.gain)
        target = MidiLearnTarget::gain;
    else
        return;

    strip.owner.showLearnMenuForTrack (strip.owner.indexOfTrack (strip.track), target);
}

MasterStrip::MasterStrip (MainComponent& ownerIn)
    : owner (ownerIn)
{
    title.setText (jp (u8"メインアウト"), juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    reverbToggle.setButtonText (jp (u8"リバーブ"));
    reverbToggle.setClickingTogglesState (true);
    reverbToggle.setToggleState (owner.getEngine().reverbEnabled.load(), juce::dontSendNotification);
    reverbToggle.onClick = [this] { owner.getEngine().reverbEnabled = reverbToggle.getToggleState(); };
    addAndMakeVisible (reverbToggle);

    limiterToggle.setButtonText (jp (u8"リミッター"));
    limiterToggle.setClickingTogglesState (true);
    limiterToggle.setToggleState (owner.getEngine().limiterEnabled.load(), juce::dontSendNotification);
    limiterToggle.onClick = [this] { owner.getEngine().limiterEnabled = limiterToggle.getToggleState(); };
    addAndMakeVisible (limiterToggle);

    auto setupSlider = [] (juce::Slider& slider, double min, double max, double step, double value, const juce::String& suffix) {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
        slider.setRange (min, max, step);
        slider.setValue (value, juce::dontSendNotification);
        slider.setTextValueSuffix (suffix);
    };

    setupSlider (reverbMix, 0.0, 1.0, 0.01, (double) owner.getEngine().reverbWet.load(), "");
    reverbMix.onValueChange = [this] { owner.getEngine().reverbWet = (float) reverbMix.getValue(); };
    addAndMakeVisible (reverbMix);
    addAndMakeVisible (reverbMixLabel);
    reverbMixLabel.setText ("Mix", juce::dontSendNotification);

    setupSlider (reverbSize, 0.0, 1.0, 0.01, (double) owner.getEngine().reverbRoom.load(), "");
    reverbSize.onValueChange = [this] { owner.getEngine().reverbRoom = (float) reverbSize.getValue(); };
    addAndMakeVisible (reverbSize);
    addAndMakeVisible (reverbSizeLabel);
    reverbSizeLabel.setText ("Size", juce::dontSendNotification);

    setupSlider (limitCeiling, -12.0, 0.0, 0.1, (double) owner.getEngine().limiterThresholdDb.load(), " dB");
    limitCeiling.onValueChange = [this] { owner.getEngine().limiterThresholdDb = (float) limitCeiling.getValue(); };
    addAndMakeVisible (limitCeiling);
    addAndMakeVisible (limitLabel);
    limitLabel.setText ("Ceiling", juce::dontSendNotification);

    masterGain.setSliderStyle (juce::Slider::LinearVertical);
    masterGain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
    masterGain.setRange (-60.0, 12.0, 0.1);
    masterGain.setValue ((double) juce::Decibels::gainToDecibels (owner.getEngine().masterGain.load(), -60.0f),
                         juce::dontSendNotification);
    masterGain.setTextValueSuffix (" dB");
    masterGain.setDoubleClickReturnValue (true, 0.0);
    masterGain.onValueChange = [this] {
        owner.getEngine().masterGain = juce::Decibels::decibelsToGain ((float) masterGain.getValue(), -60.0f);
    };
    addAndMakeVisible (masterGain);
    addAndMakeVisible (masterGainLabel);
    masterGainLabel.setText (jp (u8"出力"), juce::dontSendNotification);
    masterGainLabel.setJustificationType (juce::Justification::centred);

    addFx.setButtonText ("+ VST");
    addFx.setTooltip (jp (u8"メインアウトに VST3 を追加（最大 10）"));
    addFx.onClick = [this] { owner.promptAddPlugin ({}, true); };
    addAndMakeVisible (addFx);

    chips.onOpen = [this] (int index) {
        if (auto* plugin = owner.getEngine().masterPlugins().get (index))
            owner.openPluginEditor (*plugin);
    };
    chips.onRemove = [this] (int index) { owner.removePluginFromMaster (index); };
    chips.onBypassChanged = [this] (int index, bool bypassed) {
        owner.getEngine().masterPlugins().setBypassed (index, bypassed);
    };
    addAndMakeVisible (chips);
    addAndMakeVisible (meter);
    refreshPlugins();
}

void MasterStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
    g.setColour (juce::Colour (0xff151a26));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colour (LiteLookAndFeel::accent).withAlpha (0.35f));
    g.drawRoundedRectangle (r, 10.0f, 1.2f);
}

void MasterStrip::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto left = r.removeFromLeft (200);
    title.setBounds (left.removeFromTop (24));
    left.removeFromTop (6);
    addFx.setBounds (left.removeFromBottom (28));
    left.removeFromBottom (4);
    meter.setBounds (left.removeFromRight (14).reduced (0, 2));
    chips.setBounds (left);

    r.removeFromLeft (8);
    auto gainCol = r.removeFromRight (72);
    masterGainLabel.setBounds (gainCol.removeFromTop (18));
    masterGain.setBounds (gainCol);

    r.removeFromRight (8);
    auto row1 = r.removeFromTop (32);
    reverbToggle.setBounds (row1.removeFromLeft (96));
    reverbMixLabel.setBounds (row1.removeFromLeft (36));
    reverbMix.setBounds (row1.removeFromLeft (160));
    reverbSizeLabel.setBounds (row1.removeFromLeft (40));
    reverbSize.setBounds (row1.removeFromLeft (160));

    r.removeFromTop (8);
    auto row2 = r.removeFromTop (32);
    limiterToggle.setBounds (row2.removeFromLeft (104));
    limitLabel.setBounds (row2.removeFromLeft (58));
    limitCeiling.setBounds (row2.removeFromLeft (160));
}

void MasterStrip::refreshPlugins()
{
    juce::StringArray names;
    juce::Array<bool> active;
    auto& chain = owner.getEngine().masterPlugins();
    for (int i = 0; i < chain.size(); ++i)
    {
        if (auto* plugin = chain.get (i))
            names.add (plugin->getName());
        active.add (! chain.isBypassed (i));
    }
    chips.setPlugins (names, active);
    addFx.setEnabled (chain.canAdd());
}

void MasterStrip::syncTogglesFromEngine()
{
    reverbToggle.setToggleState (owner.getEngine().reverbEnabled.load(), juce::dontSendNotification);
    limiterToggle.setToggleState (owner.getEngine().limiterEnabled.load(), juce::dontSendNotification);
    reverbMix.setValue ((double) owner.getEngine().reverbWet.load(), juce::dontSendNotification);
    reverbSize.setValue ((double) owner.getEngine().reverbRoom.load(), juce::dontSendNotification);
    limitCeiling.setValue ((double) owner.getEngine().limiterThresholdDb.load(), juce::dontSendNotification);
    masterGain.setValue ((double) juce::Decibels::gainToDecibels (owner.getEngine().masterGain.load(), -60.0f),
                         juce::dontSendNotification);
}

void MasterStrip::setPeak (float value)
{
    meter.setLevel (value);
    meter.tick (50.0f);
}
