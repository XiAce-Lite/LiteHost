#include "PluginEditorWindow.h"
#include "LookAndFeel.h"

PluginEditorWindow::PluginEditorWindow (juce::AudioPluginInstance& instance,
                                        std::function<void (PluginEditorWindow*)> onCloseIn)
    : DocumentWindow (instance.getName(), juce::Colour (LiteLookAndFeel::surface), DocumentWindow::allButtons),
      plugin (&instance),
      onClose (std::move (onCloseIn))
{
    setUsingNativeTitleBar (true);

    if (auto* editor = instance.createEditorAndMakeActive())
        setContentOwned (editor, true);
    else
        setContentOwned (new juce::GenericAudioProcessorEditor (instance), true);

    setResizable (true, false);

    if (auto* content = getContentComponent())
        centreWithSize (juce::jmax (420, content->getWidth()),
                        juce::jmax (240, content->getHeight() + getTitleBarHeight()));
    else
        centreWithSize (480, 320);

    setVisible (true);
}

PluginEditorWindow::~PluginEditorWindow()
{
    if (plugin != nullptr)
        if (auto* editor = plugin->getActiveEditor())
            plugin->editorBeingDeleted (editor);
}

void PluginEditorWindow::closeButtonPressed()
{
    if (onClose)
        onClose (this);
}
