#include "VstFolderPathList.h"
#include "LookAndFeel.h"
#include "Utf8.h"

VstFolderPathList::PathList::PathList()
{
    setModel (this);
    setRowHeight (22);
}

void VstFolderPathList::PathList::clear()
{
    items.clear();
    selectable.clear();
    updateContent();
}

void VstFolderPathList::PathList::addItem (juce::String text, bool canSelect)
{
    items.add (std::move (text));
    selectable.add (canSelect);
    updateContent();
}

int VstFolderPathList::PathList::getNumRows()
{
    return items.size();
}

void VstFolderPathList::PathList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected && selectable[row])
        g.fillAll (juce::Colour (LiteLookAndFeel::accent).withAlpha (0.35f));

    g.setColour (juce::Colour (LiteLookAndFeel::text));
    g.setFont (LiteLookAndFeel::uiFont (13.0f));
    g.drawText (items[row], 8, 0, width - 16, height, juce::Justification::centredLeft, true);
}

void VstFolderPathList::PathList::selectedRowsChanged (int)
{
    const int row = getSelectedRow();
    if (row >= 0 && ! selectable[row])
        deselectAllRows();
}

VstFolderPathList::VstFolderPathList (juce::StringArray defaultsIn, juce::StringArray extrasIn)
    : defaults (std::move (defaultsIn)),
      extras (std::move (extrasIn))
{
    list.setMultipleSelectionEnabled (false);
    rebuildList();
    addAndMakeVisible (list);
}

void VstFolderPathList::resized()
{
    list.setBounds (getLocalBounds());
}

void VstFolderPathList::rebuildList()
{
    list.clear();
    for (const auto& path : defaults)
        list.addItem (jp (u8"[標準] ") + path, false);
    for (const auto& path : extras)
        list.addItem (path, true);
}

void VstFolderPathList::browseAndAdd()
{
    auto chooser = std::make_shared<juce::FileChooser> (jp (u8"VST3 フォルダを選択"),
                                                        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                        "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser] (const juce::FileChooser& fc) {
                              auto result = fc.getResult();
                              if (result.isDirectory())
                              {
                                  const auto path = result.getFullPathName();
                                  if (! extras.contains (path) && ! defaults.contains (path))
                                  {
                                      extras.add (path);
                                      rebuildList();
                                  }
                              }
                          });
}

void VstFolderPathList::removeSelected()
{
    const int row = list.getSelectedRow();
    if (row < defaults.size() || row >= defaults.size() + extras.size())
        return;
    extras.remove (row - defaults.size());
    rebuildList();
}
