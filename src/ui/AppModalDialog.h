#pragma once

#include <JuceHeader.h>

/** In-app modal helpers (child of MainComponent). Behaviour matches the former MainComponent locals. */
namespace AppModalDialog
{
    juce::Component* launch (juce::DialogWindow::LaunchOptions& options);

    /** Shared LaunchOptions for LiteHost settings-style panels. */
    juce::Component* launchPanel (juce::Component* centreAround,
                                  std::unique_ptr<juce::Component> content,
                                  const juce::String& title,
                                  bool resizable);

    bool forwardTabToAppModal (juce::Component& host, const juce::KeyPress& key);
    bool nudgeFocusedSlider (const juce::KeyPress& key);
    void showAlertAndFocusDefault (juce::AlertWindow* aw, juce::ModalComponentManager::Callback* callback);
    int nearestBufferSize (const juce::Array<int>& sizes, int preferred);
}
