#include "StartupSplash.h"
#include "Utf8.h"
#include "BinaryData.h"

namespace
{
    juce::Image loadAppIcon()
    {
        auto image = juce::ImageFileFormat::loadFrom (BinaryData::icon_png, BinaryData::icon_pngSize);
        if (image.isValid())
            return image;

        return {};
    }
}

StartupSplash::StartupSplash()
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setSize (460, 176);
    icon = loadAppIcon();

    title.setText ("LiteHost", juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::text));
    addAndMakeVisible (title);

    status.setText (jp (u8"起動しています..."), juce::dontSendNotification);
    status.setFont (LiteLookAndFeel::uiFont (14.0f));
    status.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    addAndMakeVisible (status);
}

void StartupSplash::setStatus (const juce::String& text, double progress01)
{
    progress = juce::jlimit (0.0, 1.0, progress01);
    status.setText (text, juce::dontSendNotification);
    repaint();
}

void StartupSplash::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (LiteLookAndFeel::bg));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (LiteLookAndFeel::accent));
    g.fillRect (0.0f, 0.0f, r.getWidth(), 3.0f);
    g.setColour (juce::Colour (0xff2c3344));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    if (icon.isValid())
    {
        const auto iconArea = juce::Rectangle<float> (20.0f, 28.0f, 88.0f, 88.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (icon, iconArea, juce::RectanglePlacement::centred);
    }

    auto bar = juce::Rectangle<float> (124.0f, 118.0f, (float) getWidth() - 148.0f, 6.0f);
    g.setColour (juce::Colour (LiteLookAndFeel::raised));
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (juce::Colour (LiteLookAndFeel::accent));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) progress), 3.0f);
}

void StartupSplash::resized()
{
    auto r = getLocalBounds().reduced (20);
    r.removeFromLeft (108);
    title.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);
    status.setBounds (r.removeFromTop (22));
}

StartupSplash::~StartupSplash()
{
    setLookAndFeel (nullptr);
}

StartupSplashWindow::StartupSplashWindow()
    : DocumentWindow ("LiteHost", juce::Colour (LiteLookAndFeel::bg), 0)
{
    setUsingNativeTitleBar (false);
    setTitleBarHeight (0);
    setResizable (false, false);
    setDropShadowEnabled (true);
    auto* content = new StartupSplash();
    splash = content;
    setContentOwned (content, true);
    centreWithSize (460, 176);
    setAlwaysOnTop (true);
    setVisible (true);
    toFront (false);
    pump (40);
}

StartupSplashWindow::~StartupSplashWindow()
{
    splash = nullptr;
}

void StartupSplashWindow::setStatus (const juce::String& text, double progress01)
{
    if (splash != nullptr)
        splash->setStatus (text, progress01);

    pump();
}

void StartupSplashWindow::pump (int milliseconds)
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        if (mm->isThisTheMessageThread())
            mm->runDispatchLoopUntil (milliseconds);
}
