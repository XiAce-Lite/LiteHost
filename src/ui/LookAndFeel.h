#pragma once

#include <JuceHeader.h>
#include <cmath>

class LiteLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr juce::uint32 bg = 0xff10131a;
    static constexpr juce::uint32 surface = 0xff1a1f2b;
    static constexpr juce::uint32 raised = 0xff252b3a;
    static constexpr juce::uint32 accent = 0xff5b8def;
    static constexpr juce::uint32 text = 0xffe8eaef;
    static constexpr juce::uint32 muted = 0xff9aa3b5;
    static constexpr juce::uint32 danger = 0xffe85d5d;
    static constexpr juce::uint32 solo = 0xffe6c35c;
    static constexpr juce::uint32 soloOverride = 0xffff9a3c;

    static juce::StringArray cjkFallbackNames()
    {
       #if JUCE_WINDOWS
        return { "Yu Gothic UI", "Meiryo UI", "Yu Gothic", "Meiryo", "MS UI Gothic", "Segoe UI" };
       #elif JUCE_MAC
        return { "Hiragino Sans", "Hiragino Kaku Gothic ProN", "Apple SD Gothic Neo" };
       #else
        return { "Noto Sans CJK JP", "Noto Sans CJK", "Source Han Sans" };
       #endif
    }

    static juce::String uiFontName()
    {
        static const juce::String name = []
        {
            const auto available = juce::Font::findAllTypefaceNames();
            for (const auto& candidate : cjkFallbackNames())
                if (available.contains (candidate))
                    return candidate;

            return juce::Font::getDefaultSansSerifFontName();
        }();

        return name;
    }

    static juce::Font uiFont (float height, int styleFlags = juce::Font::plain)
    {
        auto fallbacks = cjkFallbackNames();
        fallbacks.removeString (uiFontName());

        auto options = juce::FontOptions()
                           .withName (uiFontName())
                           .withHeight (height)
                           .withStyleFlags (styleFlags)
                           .withFallbacks (std::vector<juce::String> (fallbacks.begin(), fallbacks.end()))
                           .withFallbackEnabled (true)
                           .withMetricsKind (juce::TypefaceMetricsKind::portable);

        return juce::Font { options };
    }

    LiteLookAndFeel()
    {
        setDefaultSansSerifTypefaceName (uiFontName());

        setColourScheme ({
            juce::Colour (bg),
            juce::Colour (surface),
            juce::Colour (raised),
            juce::Colour (0xff3a4254),
            juce::Colour (text),
            juce::Colour (accent),
            juce::Colour (accent),
            juce::Colour (text),
            juce::Colour (bg)
        });

        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (bg));
        setColour (juce::TextButton::buttonColourId, juce::Colour (raised));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (accent));
        setColour (juce::TextButton::textColourOffId, juce::Colour (text));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (raised));
        setColour (juce::ComboBox::textColourId, juce::Colour (text));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a4254));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (muted));
        setColour (juce::ComboBox::focusedOutlineColourId, juce::Colour (accent));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (surface));
        setColour (juce::PopupMenu::textColourId, juce::Colour (text));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (accent));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Slider::backgroundColourId, juce::Colour (raised));
        setColour (juce::Slider::trackColourId, juce::Colour (accent));
        setColour (juce::Slider::thumbColourId, juce::Colour (text));
        setColour (juce::Label::textColourId, juce::Colour (text));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (raised));
        setColour (juce::TextEditor::textColourId, juce::Colour (text));
        setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a4254));
        setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (accent));
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (surface));
        setColour (juce::AlertWindow::textColourId, juce::Colour (text));
        setColour (juce::AlertWindow::outlineColourId, juce::Colour (0xff3a4254));
        setUsingNativeAlertWindows (false);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3a4254));
        setColour (juce::ToggleButton::textColourId, juce::Colour (text));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (accent));
        setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (muted));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return uiFont (14.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (14.0f); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    juce::Font getAlertWindowTitleFont() override { return uiFont (16.0f, juce::Font::bold); }
    juce::Font getAlertWindowMessageFont() override { return uiFont (14.0f); }
    juce::Font getAlertWindowFont() override { return uiFont (14.0f); }
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override { return uiFont (14.0f); }
    juce::Font getSliderPopupFont (juce::Slider&) override { return uiFont (13.0f); }

    /** Avoid Alt+Tab / taskbar entries for OK/Cancel style alerts. */
    int getAlertBoxWindowFlags() override
    {
        // Keep off the taskbar; avoid windowIsTemporary (breaks keyboard focus / Tab).
        return juce::ComponentPeer::windowHasDropShadow;
    }

    static bool isAlertDefaultButton (const juce::Button& button)
    {
        auto* alert = button.findParentComponentOfClass<juce::AlertWindow>();
        if (alert == nullptr)
            return false;

        for (int i = 0; i < alert->getNumButtons(); ++i)
            if (auto* b = alert->getButton (i); b != nullptr && b->hasKeyboardFocus (true))
                return false;

        return button.isRegisteredForShortcut (juce::KeyPress (juce::KeyPress::returnKey));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override
    {
        juce::ignoreUnused (isButtonDown, buttonX, buttonY, buttonW, buttonH);

        auto corner = 6.0f;
        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, corner);

        const bool focused = box.hasKeyboardFocus (true);
        g.setColour (focused ? juce::Colour (accent) : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, corner, focused ? 1.6f : 1.0f);

        auto arrowZone = bounds.removeFromRight (20.0f).reduced (0.0f, 8.0f);
        juce::Path path;
        path.startNewSubPath (arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
        path.lineTo (arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
        path.lineTo (arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (path, juce::PathStrokeType (1.6f));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        LookAndFeel_V4::drawToggleButton (g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        if (button.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (accent));
            g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.6f);
        }
    }

    /** Focus = outline only (never fill), so ON colours like Reverb stay distinct. */
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        const bool focused = button.hasKeyboardFocus (true);
        const bool alertDefault = isAlertDefaultButton (button);
        auto baseColour = backgroundColour;

        // Primary CTA fill only for unfocused Alert default (Enter target).
        if (alertDefault && ! focused && ! button.getToggleState())
            baseColour = juce::Colour (accent);

        if (! button.isEnabled())
            baseColour = baseColour.withMultipliedAlpha (0.45f);
        else if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker (0.18f);
        else if (shouldDrawButtonAsHighlighted && ! focused)
            baseColour = baseColour.brighter (0.12f);

        constexpr float corner = 6.0f;
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f, 0.5f);

        g.setColour (baseColour);
        g.fillRoundedRectangle (bounds, corner);

        if (focused || alertDefault)
        {
            g.setColour (focused ? juce::Colour (accent) : juce::Colours::white.withAlpha (0.55f));
            g.drawRoundedRectangle (bounds.reduced (focused ? 0.0f : 1.0f),
                                   corner - (focused ? 0.0f : 1.0f),
                                   focused ? 2.0f : 1.6f);
        }
        else
        {
            g.setColour (juce::Colour (0xff3a4254));
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        const bool alertDefault = isAlertDefaultButton (button) && ! button.hasKeyboardFocus (true);
        const bool on = button.getToggleState();
        auto font = getTextButtonFont (button, button.getHeight());
        g.setFont (font);

        if (on || alertDefault)
            g.setColour (juce::Colours::white.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        else
            g.setColour (button.findColour (juce::TextButton::textColourOffId)
                             .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

        const auto yIndent = juce::jmin (4, button.proportionOfHeight (0.3f));
        const auto cornerSize = juce::jmin (button.getHeight(), button.getWidth()) / 2;
        const auto fontHeight = juce::roundToInt (font.getHeight() * 0.6f);
        const auto leftIndent = juce::jmin (fontHeight, 2 + cornerSize / (button.isConnectedOnLeft() ? 4 : 2));
        const auto rightIndent = juce::jmin (fontHeight, 2 + cornerSize / (button.isConnectedOnRight() ? 4 : 2));

        g.drawFittedText (button.getButtonText(),
                          leftIndent,
                          yIndent,
                          button.getWidth() - leftIndent - rightIndent,
                          button.getHeight() - yIndent * 2,
                          juce::Justification::centred,
                          2);
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        /** Slider forces setEditable(true) (= single-click) after createSliderTextBox, which
            opens the TextEditor on Tab and steals arrow keys. Keep double-click-to-type and
            Tab-focus for ↑↓ nudge instead. */
        struct ValueBox final : public juce::Label
        {
            void preferKeyboardNudgeMode()
            {
                if (isBeingEdited())
                    return;

                if (isEditableOnSingleClick() || (isEditable() && ! isEditableOnDoubleClick()))
                    setEditable (false, true, true);

                setWantsKeyboardFocus (true);
                setFocusContainerType (juce::Component::FocusContainerType::none);
            }

            void parentHierarchyChanged() override
            {
                juce::Label::parentHierarchyChanged();
                preferKeyboardNudgeMode();
            }

            void visibilityChanged() override
            {
                juce::Label::visibilityChanged();
                preferKeyboardNudgeMode();
            }

            void focusGained (juce::Component::FocusChangeType cause) override
            {
                preferKeyboardNudgeMode();
                // Skip Label::focusGained — it opens the editor when Tab-focusing single-click labels.
                juce::Component::focusGained (cause);
                repaint();
            }

            void mouseUp (const juce::MouseEvent& e) override
            {
                preferKeyboardNudgeMode();
                // Skip Label::mouseUp single-click edit; click focuses for arrow keys.
                juce::Component::mouseUp (e);
            }

            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                preferKeyboardNudgeMode();
                juce::Label::mouseDoubleClick (e);
            }

            bool keyPressed (const juce::KeyPress& key) override
            {
                if (isBeingEdited())
                    return juce::Label::keyPressed (key);

                if (auto* s = findParentComponentOfClass<juce::Slider>())
                {
                    double step = s->getInterval();
                    if (step <= 0.0)
                        step = 0.1;
                    if (key.getModifiers().isShiftDown())
                        step *= 10.0;

                    if (key.isKeyCode (juce::KeyPress::upKey)
                        || key.isKeyCode (juce::KeyPress::rightKey))
                    {
                        s->setValue (s->getValue() + step, juce::sendNotificationSync);
                        return true;
                    }

                    if (key.isKeyCode (juce::KeyPress::downKey)
                        || key.isKeyCode (juce::KeyPress::leftKey))
                    {
                        s->setValue (s->getValue() - step, juce::sendNotificationSync);
                        return true;
                    }
                }

                return juce::Label::keyPressed (key);
            }

            void paint (juce::Graphics& g) override
            {
                juce::Label::paint (g);
                if (hasKeyboardFocus (true) && ! isBeingEdited())
                {
                    g.setColour (juce::Colour (accent));
                    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.6f);
                }
            }
        };

        auto* l = new ValueBox();
        l->setJustificationType (juce::Justification::centred);
        l->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
        l->setColour (juce::Label::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        l->setColour (juce::Label::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        l->setColour (juce::Label::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        l->setColour (juce::TextEditor::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        l->setColour (juce::TextEditor::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        l->setColour (juce::TextEditor::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        l->setColour (juce::TextEditor::highlightColourId, slider.findColour (juce::Slider::textBoxHighlightColourId));
        l->setWantsKeyboardFocus (true);
        return l;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float midAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
        const float valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto spoke = [cx, cy] (float angle, float inner, float outer) {
            const auto s = std::sin (angle);
            const auto c = std::cos (angle);
            return juce::Line<float> (cx + inner * s, cy - inner * c, cx + outer * s, cy - outer * c);
        };

        g.setColour (juce::Colour (muted));
        g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.4f);

        g.setColour (juce::Colour (text));
        g.drawLine (spoke (midAngle, radius - 6.0f, radius + 1.0f), 1.6f);

        g.setColour (juce::Colour (text).withAlpha (0.92f));
        g.drawLine (spoke (valueAngle, 4.0f, radius - 5.0f), 1.8f);
        g.fillEllipse (cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
    }

    juce::Label* createComboBoxTextBox (juce::ComboBox& box) override
    {
        auto* label = juce::LookAndFeel_V4::createComboBoxTextBox (box);
        label->setFont (uiFont (14.0f));
        return label;
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll (label.findColour (juce::Label::backgroundColourId));

        if (! label.isBeingEdited())
        {
            auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (getLabelFont (label));
            g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                              juce::jmax (1, (int) ((float) textArea.getHeight() / getLabelFont (label).getHeight())),
                              label.getMinimumHorizontalScale());
        }
    }
};
