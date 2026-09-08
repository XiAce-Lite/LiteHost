#include "SettingsDialogs.h"
#include "LookAndFeel.h"
#include "Utf8.h"

SurfaceSettingsPanel::SurfaceSettingsPanel (ControlSurfaceManager& surfaceIn)
    : surface (surfaceIn)
{
    title.setText (jp (u8"コントロールサーフェス"), juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    hint.setText (jp (u8"演奏用 MIDI とは別に、サーフェス用ポートとプロトコルを指定します。"),
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (hint);

    enabledToggle.setButtonText (jp (u8"有効"));
    enabledToggle.setClickingTogglesState (true);
    enabledToggle.setToggleState (surface.isEnabled(), juce::dontSendNotification);
    addAndMakeVisible (enabledToggle);

    protocolLabel.setText (jp (u8"プロトコル"), juce::dontSendNotification);
    addAndMakeVisible (protocolLabel);
    protocolBox.addItem ("Mackie Control", 1);
    protocolBox.addItem ("HUI", 2);
    protocolBox.addItem ("MMC", 3);
    protocolBox.setSelectedId ((int) surface.getProtocol() + 1, juce::dontSendNotification);
    addAndMakeVisible (protocolBox);

    inLabel.setText (jp (u8"MIDI 入力"), juce::dontSendNotification);
    addAndMakeVisible (inLabel);
    outLabel.setText (jp (u8"MIDI 出力（LED 等）"), juce::dontSendNotification);
    addAndMakeVisible (outLabel);

    inBox.addItem (jp (u8"(なし)"), 1);
    outBox.addItem (jp (u8"(なし)"), 1);

    const auto inputs = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < inputs.size(); ++i)
    {
        inBox.addItem (inputs[i].name, i + 2);
        if (inputs[i].identifier == surface.getInputDeviceIdentifier())
            inBox.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (inBox.getSelectedId() == 0)
        inBox.setSelectedId (1, juce::dontSendNotification);

    const auto outputs = juce::MidiOutput::getAvailableDevices();
    for (int i = 0; i < outputs.size(); ++i)
    {
        outBox.addItem (outputs[i].name, i + 2);
        if (outputs[i].identifier == surface.getOutputDeviceIdentifier())
            outBox.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (outBox.getSelectedId() == 0)
        outBox.setSelectedId (1, juce::dontSendNotification);

    addAndMakeVisible (inBox);
    addAndMakeVisible (outBox);

    ok.setButtonText (jp (u8"OK"));
    ok.onClick = [this] {
        surface.setEnabled (enabledToggle.getToggleState());
        surface.setProtocol ((ControlSurfaceProtocol) (protocolBox.getSelectedId() - 1));

        juce::String inId, outId;
        const int inSel = inBox.getSelectedId();
        if (inSel >= 2)
        {
            const auto devices = juce::MidiInput::getAvailableDevices();
            if (juce::isPositiveAndBelow (inSel - 2, devices.size()))
                inId = devices[inSel - 2].identifier;
        }
        const int outSel = outBox.getSelectedId();
        if (outSel >= 2)
        {
            const auto devices = juce::MidiOutput::getAvailableDevices();
            if (juce::isPositiveAndBelow (outSel - 2, devices.size()))
                outId = devices[outSel - 2].identifier;
        }
        surface.setInputDeviceIdentifier (inId);
        surface.setOutputDeviceIdentifier (outId);
        surface.applySettings();
        if (onOk)
            onOk();
        if (onClose)
            onClose();
    };
    addAndMakeVisible (ok);

    close.setButtonText (jp (u8"閉じる"));
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

void SurfaceSettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (24));
    hint.setBounds (r.removeFromTop (36));
    r.removeFromTop (8);
    enabledToggle.setBounds (r.removeFromTop (28).removeFromLeft (120));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    protocolLabel.setBounds (row.removeFromLeft (100));
    protocolBox.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (28);
    inLabel.setBounds (row.removeFromLeft (100));
    inBox.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (28);
    outLabel.setBounds (row.removeFromLeft (140));
    outBox.setBounds (row);
    auto buttons = r.removeFromBottom (36);
    close.setBounds (buttons.removeFromRight (90).reduced (2));
    ok.setBounds (buttons.removeFromRight (90).reduced (2));
}

MidiLearnSettingsPanel::MidiLearnSettingsPanel (MidiLearnManager& learnIn)
    : learn (learnIn)
{
    title.setText (jp (u8"MIDI 学習"), juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    hint.setText (jp (u8"学習用 MIDI 入力を選び、トラックの Trim/フェーダー/Pan/M/S、またはメインアウトのフェーダー・リバーブ・リミッター・ゲートを右クリックして学習します。"),
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (hint);

    enabledToggle.setButtonText (jp (u8"有効"));
    enabledToggle.setClickingTogglesState (true);
    enabledToggle.setToggleState (learn.isEnabled(), juce::dontSendNotification);
    addAndMakeVisible (enabledToggle);

    inLabel.setText (jp (u8"MIDI 入力"), juce::dontSendNotification);
    addAndMakeVisible (inLabel);
    inBox.addItem (jp (u8"(なし)"), 1);
    const auto inputs = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < inputs.size(); ++i)
    {
        inBox.addItem (inputs[i].name, i + 2);
        if (inputs[i].identifier == learn.getInputDeviceIdentifier())
            inBox.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (inBox.getSelectedId() == 0)
        inBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (inBox);

    clearAll.setButtonText (jp (u8"割り当てを全消去"));
    clearAll.onClick = [this] { learn.clearAll(); };
    addAndMakeVisible (clearAll);

    ok.setButtonText (jp (u8"OK"));
    ok.onClick = [this] {
        learn.setEnabled (enabledToggle.getToggleState());
        juce::String inId;
        const int inSel = inBox.getSelectedId();
        if (inSel >= 2)
        {
            const auto devices = juce::MidiInput::getAvailableDevices();
            if (juce::isPositiveAndBelow (inSel - 2, devices.size()))
                inId = devices[inSel - 2].identifier;
        }
        learn.setInputDeviceIdentifier (inId);
        learn.applySettings();
        if (onOk)
            onOk();
        if (onClose)
            onClose();
    };
    addAndMakeVisible (ok);

    close.setButtonText (jp (u8"閉じる"));
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

void MidiLearnSettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (24));
    hint.setBounds (r.removeFromTop (48));
    r.removeFromTop (8);
    enabledToggle.setBounds (r.removeFromTop (28).removeFromLeft (120));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    inLabel.setBounds (row.removeFromLeft (100));
    inBox.setBounds (row);
    r.removeFromTop (12);
    clearAll.setBounds (r.removeFromTop (28).removeFromLeft (160));
    auto buttons = r.removeFromBottom (36);
    close.setBounds (buttons.removeFromRight (90).reduced (2));
    ok.setBounds (buttons.removeFromRight (90).reduced (2));
}

OptionsGeneralPanel::OptionsGeneralPanel (bool exclusiveSolo, bool confirmQuit)
{
    title.setText (jp (u8"一般"), juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    exclusiveSoloToggle.setButtonText (jp (u8"排他ソロ"));
    exclusiveSoloToggle.setClickingTogglesState (true);
    exclusiveSoloToggle.setToggleState (exclusiveSolo, juce::dontSendNotification);
    exclusiveSoloToggle.setTooltip (jp (u8"Exclusive Solo（Cakewalk）\nON: ソロは1本だけ。次に S を押したトラック以外は解除。\nShift+S の Override は残る。OFF にした瞬間は今のソロを変えない。"));
    addAndMakeVisible (exclusiveSoloToggle);

    exclusiveHint.setText (jp (u8"ON のとき、通常のソロは1トラックだけ有効になります（Shift+ソロの Override は残ります）。"),
                           juce::dontSendNotification);
    exclusiveHint.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (exclusiveHint);

    confirmQuitToggle.setButtonText (jp (u8"終了時に確認する"));
    confirmQuitToggle.setClickingTogglesState (true);
    confirmQuitToggle.setToggleState (confirmQuit, juce::dontSendNotification);
    addAndMakeVisible (confirmQuitToggle);

    quitHint.setText (jp (u8"ON のとき、アプリ終了前に OK / キャンセルを表示します（既定はキャンセル）。"),
                      juce::dontSendNotification);
    quitHint.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (quitHint);

    ok.setButtonText (jp (u8"OK"));
    ok.onClick = [this] {
        if (onOk)
            onOk();
        if (onClose)
            onClose();
    };
    addAndMakeVisible (ok);

    close.setButtonText (jp (u8"閉じる"));
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

void OptionsGeneralPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    exclusiveSoloToggle.setBounds (r.removeFromTop (28).removeFromLeft (200));
    exclusiveHint.setBounds (r.removeFromTop (40));
    r.removeFromTop (12);
    confirmQuitToggle.setBounds (r.removeFromTop (28).removeFromLeft (220));
    quitHint.setBounds (r.removeFromTop (40));
    auto buttons = r.removeFromBottom (36);
    close.setBounds (buttons.removeFromRight (90).reduced (2));
    ok.setBounds (buttons.removeFromRight (90).reduced (2));
}
