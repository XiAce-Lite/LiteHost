#include "MixerWidgets.h"
#include "LookAndFeel.h"
#include "Utf8.h"

void LevelMeter::setLevel (float linear)
{
    level = juce::jlimit (0.0f, 2.0f, linear);
    if (level > hold)
    {
        hold = level;
        holdMs = 600.0f;
    }
    repaint();
}

void LevelMeter::tick (float deltaMs)
{
    if (holdMs > 0.0f)
    {
        holdMs -= deltaMs;
        if (holdMs <= 0.0f)
            hold = level;
    }
    else
    {
        hold *= 0.92f;
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff10131a));
    g.fillRoundedRectangle (r, 3.0f);

    const float shown = juce::jmax (level, hold);
    const float db = juce::Decibels::gainToDecibels (shown, -60.0f);
    const float norm = juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, 0.0f, 1.0f);
    auto fill = r.removeFromBottom (r.getHeight() * norm);

    // Yellow from -12 dB so drum transients are easier to see.
    auto colour = juce::Colour (0xff3dd68c);
    if (db > -1.0f)
        colour = juce::Colour (LiteLookAndFeel::danger);
    else if (db > -12.0f)
        colour = juce::Colour (LiteLookAndFeel::solo);

    g.setColour (colour);
    g.fillRoundedRectangle (fill, 3.0f);
}

void PluginChipBar::setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags)
{
    names = namesToUse;
    buttons.clear();
    removeButtons.clear();

    for (int i = 0; i < names.size(); ++i)
    {
        auto* toggle = buttons.add (new juce::TextButton (names[i]));
        toggle->setClickingTogglesState (true);
        toggle->setToggleState (i < activeFlags.size() ? activeFlags[i] : true, juce::dontSendNotification);
        toggle->setTooltip (jp (u8"クリック: オン/オフ  ダブルクリック: エディタ"));
        toggle->setColour (juce::TextButton::buttonOnColourId, juce::Colour (LiteLookAndFeel::accent));
        toggle->onClick = [this, i, toggle] {
            if (onBypassChanged)
                onBypassChanged (i, ! toggle->getToggleState());
        };
        toggle->onStateChange = {}; // keep default
        addAndMakeVisible (toggle);

        // Double-click opens editor; two single clicks from a double-click
        // cancel each other out for the toggle state.
        toggle->addMouseListener (this, false);

        auto* remove = removeButtons.add (new juce::TextButton ("x"));
        remove->setTooltip (jp (u8"プラグインを外す"));
        remove->onClick = [this, i] { if (onRemove) onRemove (i); };
        addAndMakeVisible (remove);
    }

    resized();
}

void PluginChipBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int i = 0; i < buttons.size(); ++i)
    {
        if (e.eventComponent == buttons[i])
        {
            if (onOpen)
                onOpen (i);
            return;
        }
    }
}

int PluginChipBar::getPreferredHeight() const
{
    return buttons.isEmpty() ? 0 : buttons.size() * 30 + juce::jmax (0, buttons.size() - 1) * 4;
}

void PluginChipBar::resized()
{
    auto r = getLocalBounds();
    for (int i = 0; i < buttons.size(); ++i)
    {
        auto row = r.removeFromTop (30);
        removeButtons[i]->setBounds (row.removeFromRight (26).reduced (1));
        buttons[i]->setBounds (row.reduced (1));
        r.removeFromTop (4);
    }
}

PluginChipList::PluginChipList()
{
    viewport.setViewedComponent (&chips, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    chips.onOpen = [this] (int i) { if (onOpen) onOpen (i); };
    chips.onRemove = [this] (int i) { if (onRemove) onRemove (i); };
    chips.onBypassChanged = [this] (int i, bool b) { if (onBypassChanged) onBypassChanged (i, b); };
}

void PluginChipList::setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags)
{
    chips.setPlugins (namesToUse, activeFlags);
    layoutChips();
}

void PluginChipList::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutChips();
}

void PluginChipList::layoutChips()
{
    const int viewH = juce::jmax (1, viewport.getHeight());
    const int barW = juce::jmax (1, viewport.getMaximumVisibleWidth());
    chips.setSize (barW, juce::jmax (viewH, chips.getPreferredHeight()));
}
