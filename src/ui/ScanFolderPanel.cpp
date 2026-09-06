#include "ScanFolderPanel.h"
#include "LookAndFeel.h"
#include "Utf8.h"
#include "app/PluginScanCoordinator.h"

ScanFolderPanel::ScanFolderPanel (juce::StringArray defaultsIn, juce::StringArray extrasIn)
    : list (std::move (defaultsIn), std::move (extrasIn))
{
    title.setText (jp (u8"VST3 スキャン対象"), juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    hint.setText (PluginScanCoordinator::defaultFolderHint(), juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (hint);

    addAndMakeVisible (list);

    addFolder.setButtonText (jp (u8"フォルダを追加"));
    addFolder.onClick = [this] { list.browseAndAdd(); };
    addAndMakeVisible (addFolder);

    removeFolder.setButtonText (jp (u8"選択を削除"));
    removeFolder.onClick = [this] { list.removeSelected(); };
    addAndMakeVisible (removeFolder);

    scan.setButtonText (jp (u8"スキャン開始"));
    scan.onClick = [this] { if (onScan) onScan(); };
    addAndMakeVisible (scan);

    cancel.setButtonText (jp (u8"閉じる"));
    cancel.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (cancel);
}

void ScanFolderPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (24));
    hint.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);
    auto buttons = r.removeFromBottom (36);
    list.setBounds (r.reduced (0, 4));
    addFolder.setBounds (buttons.removeFromLeft (130).reduced (2));
    removeFolder.setBounds (buttons.removeFromLeft (110).reduced (2));
    cancel.setBounds (buttons.removeFromRight (90).reduced (2));
    scan.setBounds (buttons.removeFromRight (120).reduced (2));
}
