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

//==============================================================================
PluginChipBar::Chip::Chip (PluginChipBar& ownerIn, int indexIn, juce::String nameIn, bool activeIn)
    : owner (ownerIn),
      index (indexIn),
      name (std::move (nameIn)),
      active (activeIn)
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (jp (u8"クリック: オン/オフ  ダブルクリック: エディタ\nドラッグ: 他トラックへ移動  Ctrl+ドラッグ: コピー"));
}

void PluginChipBar::Chip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hover = isMouseOver (true);

    auto fill = active ? juce::Colour (LiteLookAndFeel::accent)
                       : juce::Colour (LiteLookAndFeel::raised);
    if (hover)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (0xff2c3344));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    g.setColour (active ? juce::Colours::white : juce::Colour (LiteLookAndFeel::muted));
    g.setFont (LiteLookAndFeel::uiFont (13.0f));
    g.drawText (name, r.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
}

void PluginChipBar::Chip::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
    suppressClick = false;
}

void PluginChipBar::Chip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 5)
        return;

    if (owner.onDragStart != nullptr && owner.onDragStart (index, *this))
    {
        dragStarted = true;
        suppressClick = true;
    }
}

void PluginChipBar::Chip::mouseUp (const juce::MouseEvent& e)
{
    if (suppressClick || dragStarted || e.mouseWasDraggedSinceMouseDown())
        return;

    active = ! active;
    repaint();
    if (owner.onBypassChanged)
        owner.onBypassChanged (index, ! active);
}

void PluginChipBar::Chip::mouseDoubleClick (const juce::MouseEvent&)
{
    if (owner.onOpen)
        owner.onOpen (index);
}

void PluginChipBar::setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags)
{
    chips.clear();
    removeButtons.clear();

    for (int i = 0; i < namesToUse.size(); ++i)
    {
        const bool active = i < activeFlags.size() ? activeFlags[i] : true;
        chips.add (new Chip (*this, i, namesToUse[i], active));
        addAndMakeVisible (chips.getLast());

        auto* remove = removeButtons.add (new juce::TextButton ("x"));
        remove->setTooltip (jp (u8"プラグインを外す"));
        remove->onClick = [this, i] { if (onRemove) onRemove (i); };
        addAndMakeVisible (remove);
    }

    resized();
}

int PluginChipBar::getPreferredHeight() const
{
    return chips.isEmpty() ? 0 : chips.size() * 30 + juce::jmax (0, chips.size() - 1) * 4;
}

void PluginChipBar::resized()
{
    auto r = getLocalBounds();
    for (int i = 0; i < chips.size(); ++i)
    {
        auto row = r.removeFromTop (30);
        removeButtons[i]->setBounds (row.removeFromRight (26).reduced (1));
        chips[i]->setBounds (row.reduced (1));
        r.removeFromTop (4);
    }
}

//==============================================================================
PluginChipList::PluginChipList()
{
    viewport.setViewedComponent (&chips, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible (viewport);

    chips.onOpen = [this] (int i) { if (onOpen) onOpen (i); };
    chips.onRemove = [this] (int i) { if (onRemove) onRemove (i); };
    chips.onBypassChanged = [this] (int i, bool b) { if (onBypassChanged) onBypassChanged (i, b); };
    chips.onDragStart = [this] (int i, juce::Component& source) -> bool {
        if (onDragStart)
            return onDragStart (i, source);
        return false;
    };
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
