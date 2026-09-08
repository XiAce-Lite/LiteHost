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
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (surface));
        setColour (juce::PopupMenu::textColourId, juce::Colour (text));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (accent));
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

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const auto midAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
        const auto valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto spoke = [cx, cy] (float angle, float inner, float outer) {
            const auto s = std::sin (angle);
            const auto c = std::cos (angle);
            return juce::Line<float> (cx + inner * s, cy - inner * c, cx + outer * s, cy - outer * c);
        };

        g.setColour (juce::Colour (muted));
        g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.4f);

        // Centre-of-travel tick (short radial mark; vertical when the range centre is 12 o'clock).
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
