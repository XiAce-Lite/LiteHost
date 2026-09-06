#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"

class StartupProgress
{
public:
    virtual ~StartupProgress() = default;
    virtual void setStatus (const juce::String& text, double progress01) = 0;
};

class StartupSplash : public juce::Component
{
public:
    StartupSplash();
    ~StartupSplash() override;

    void setStatus (const juce::String& text, double progress01);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    LiteLookAndFeel lookAndFeel;
    juce::Image icon;
    juce::Label title, status;
    double progress = 0.0;
};

class StartupSplashWindow : public juce::DocumentWindow,
                            public StartupProgress
{
public:
    StartupSplashWindow();
    ~StartupSplashWindow() override;

    void setStatus (const juce::String& text, double progress01) override;
    static void pump (int milliseconds = 8);

private:
    StartupSplash* splash = nullptr;
};
