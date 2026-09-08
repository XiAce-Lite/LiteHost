#pragma once

#include <JuceHeader.h>

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

    static juce::StringArray cjkFallbackNames();
    static juce::String uiFontName();
    static juce::Font uiFont (float height, int styleFlags = juce::Font::plain);

    LiteLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;

    /** Avoid Alt+Tab / taskbar entries for OK/Cancel style alerts. */
    int getAlertBoxWindowFlags() override;

    static bool isAlertDefaultButton (const juce::Button& button);

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** Focus = outline only (never fill), so ON colours like Reverb stay distinct. */
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Label* createSliderTextBox (juce::Slider& slider) override;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider&) override;

    juce::Label* createComboBoxTextBox (juce::ComboBox& box) override;

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};
