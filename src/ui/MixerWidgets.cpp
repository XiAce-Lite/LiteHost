#include "MixerWidgets.h"
#include "LookAndFeel.h"
#include "Utf8.h"

namespace
{
    constexpr int chipRowHeight = 30;
    constexpr int chipRowGap = 4;
    constexpr int chipStride = chipRowHeight + chipRowGap;
    constexpr const char* pluginDragPrefix = "litehost-plugin";
}

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
ChainPowerButton::ChainPowerButton()
    : juce::Button ("chainPower")
{
    setClickingTogglesState (true);
    setTooltip (jp (u8"トラックの VST チェイン全体をオン/オフ"));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ChainPowerButton::paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState();

    auto fill = on ? juce::Colour (LiteLookAndFeel::accent)
                   : juce::Colour (LiteLookAndFeel::raised);
    if (isButtonDown)
        fill = fill.darker (0.12f);
    else if (isMouseOverButton)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (0xff2c3344));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const auto cx = r.getCentreX();
    const auto cy = r.getCentreY();
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.22f;
    auto colour = on ? juce::Colours::white : juce::Colour (LiteLookAndFeel::muted);
    g.setColour (colour);

    juce::Path arc;
    arc.addCentredArc (cx, cy + 0.5f, radius, radius, 0.0f,
                       juce::MathConstants<float>::pi * 0.28f,
                       juce::MathConstants<float>::pi * 1.72f,
                       true);
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.drawLine (cx, cy - radius - 1.5f, cx, cy + 0.5f, 2.0f);
}

//==============================================================================
PluginChipBar::Chip::Chip (PluginChipBar& ownerIn, int indexIn, juce::String nameIn, bool activeIn)
    : owner (ownerIn),
      index (indexIn),
      name (std::move (nameIn)),
      active (activeIn)
{
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    updateTooltip();
}

void PluginChipBar::Chip::updateTooltip()
{
    const auto& help = owner.chipHelpText;
    const float avail = (float) getWidth() - 16.0f;
    const auto font = LiteLookAndFeel::uiFont (13.0f);
    const bool truncated = getWidth() <= 0
                        || (avail > 0.0f
                            && juce::GlyphArrangement::getStringWidth (font, name) > avail);
    // Always lead with the full name when the chip may be clipping it.
    if (truncated)
        setTooltip (help.isNotEmpty() ? (name + "\n" + help) : name);
    else
        setTooltip (help);
}

void PluginChipBar::Chip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hover = isMouseOver (true);

    auto fill = active ? juce::Colour (LiteLookAndFeel::accent)
                       : juce::Colour (LiteLookAndFeel::raised);
    auto text = active ? juce::Colours::white : juce::Colour (LiteLookAndFeel::muted);

    // Whole-chain bypass: keep individual on/off semantics, but dim the "on" chips.
    if (active && owner.chainBypassed)
    {
        fill = fill.withMultipliedBrightness (0.42f).withMultipliedAlpha (0.85f);
        text = text.withMultipliedAlpha (0.7f);
    }

    if (hover)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (hasKeyboardFocus (true) ? juce::Colour (LiteLookAndFeel::accent)
                                         : juce::Colour (0xff2c3344));
    g.drawRoundedRectangle (r, 4.0f, hasKeyboardFocus (true) ? 1.6f : 1.0f);

    g.setColour (text);
    g.setFont (LiteLookAndFeel::uiFont (13.0f));
    g.drawText (name, r.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
}

void PluginChipBar::Chip::resized()
{
    updateTooltip();
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

bool PluginChipBar::Chip::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
    {
        active = ! active;
        repaint();
        if (owner.onBypassChanged)
            owner.onBypassChanged (index, ! active);
        return true;
    }

    return false;
}

void PluginChipBar::Chip::focusGained (juce::Component::FocusChangeType)
{
    repaint();
}

void PluginChipBar::Chip::focusLost (juce::Component::FocusChangeType)
{
    repaint();
}

void PluginChipBar::setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags)
{
    chips.clear();
    removeButtons.clear();
    dropInsertIndex = -1;

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

void PluginChipBar::setChainBypassed (bool shouldBypass)
{
    if (chainBypassed == shouldBypass)
        return;

    chainBypassed = shouldBypass;
    for (auto* chip : chips)
        chip->repaint();
}

void PluginChipBar::setChipHelpText (juce::String text)
{
    if (chipHelpText == text)
        return;

    chipHelpText = std::move (text);
    for (auto* chip : chips)
        chip->updateTooltip();
}

int PluginChipBar::getPreferredHeight() const
{
    return chips.isEmpty() ? 0 : chips.size() * chipRowHeight + juce::jmax (0, chips.size() - 1) * chipRowGap;
}

void PluginChipBar::paint (juce::Graphics& g)
{
    if (dropInsertIndex < 0)
        return;

    const float y = (float) (dropInsertIndex * chipStride) - (float) chipRowGap * 0.5f;
    g.setColour (juce::Colour (LiteLookAndFeel::accent));
    g.fillRoundedRectangle (2.0f, juce::jmax (0.0f, y), (float) getWidth() - 4.0f, 3.0f, 1.5f);
}

void PluginChipBar::resized()
{
    auto r = getLocalBounds();
    // Per plugin: chip (on/off) then delete — logical Tab order within the bar.
    int focusOrder = 1;
    for (int i = 0; i < chips.size(); ++i)
    {
        auto row = r.removeFromTop (chipRowHeight);
        removeButtons[i]->setBounds (row.removeFromRight (26).reduced (1));
        chips[i]->setBounds (row.reduced (1));

        chips[i]->setWantsKeyboardFocus (true);
        chips[i]->setExplicitFocusOrder (focusOrder++);
        removeButtons[i]->setWantsKeyboardFocus (true);
        removeButtons[i]->setExplicitFocusOrder (focusOrder++);

        r.removeFromTop (chipRowGap);
    }
}

bool PluginChipBar::isPluginDrag (const SourceDetails& details) noexcept
{
    return details.description.toString().startsWith (pluginDragPrefix);
}

int PluginChipBar::insertIndexFromY (int y) const noexcept
{
    if (chips.isEmpty())
        return 0;

    if (y <= 0)
        return 0;

    const int raw = y / chipStride;
    const int within = y % chipStride;
    int index = raw + (within > chipRowHeight / 2 ? 1 : 0);
    return juce::jlimit (0, chips.size(), index);
}

bool PluginChipBar::isInterestedInDragSource (const SourceDetails& details)
{
    return isPluginDrag (details);
}

void PluginChipBar::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void PluginChipBar::itemDragMove (const SourceDetails& details)
{
    if (! isPluginDrag (details))
        return;

    const int next = insertIndexFromY (details.localPosition.y);
    if (next != dropInsertIndex)
    {
        dropInsertIndex = next;
        repaint();
    }
}

void PluginChipBar::itemDragExit (const SourceDetails&)
{
    dropInsertIndex = -1;
    repaint();
}

void PluginChipBar::itemDropped (const SourceDetails& details)
{
    const int insertIndex = dropInsertIndex >= 0 ? dropInsertIndex
                                                 : insertIndexFromY (details.localPosition.y);
    dropInsertIndex = -1;
    repaint();

    if (! isPluginDrag (details) || onPluginDrop == nullptr)
        return;

    onPluginDrop (insertIndex, details.description);
}

//==============================================================================
PluginChipList::PluginChipList()
{
    viewport.setViewedComponent (&chips, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);

    chips.onOpen = [this] (int i) { if (onOpen) onOpen (i); };
    chips.onRemove = [this] (int i) { if (onRemove) onRemove (i); };
    chips.onBypassChanged = [this] (int i, bool b) { if (onBypassChanged) onBypassChanged (i, b); };
    chips.onDragStart = [this] (int i, juce::Component& source) -> bool {
        if (onDragStart)
            return onDragStart (i, source);
        return false;
    };
    chips.onPluginDrop = [this] (int insertIndex, const juce::var& description) {
        if (onPluginDrop)
            onPluginDrop (insertIndex, description);
    };
}

void PluginChipList::setPlugins (const juce::StringArray& namesToUse, const juce::Array<bool>& activeFlags)
{
    chips.setPlugins (namesToUse, activeFlags);
    layoutChips();
}

void PluginChipList::setChainBypassed (bool shouldBypass)
{
    chips.setChainBypassed (shouldBypass);
}

void PluginChipList::setChipHelpText (juce::String text)
{
    chips.setChipHelpText (std::move (text));
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
