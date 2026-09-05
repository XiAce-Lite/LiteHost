#pragma once

#include <JuceHeader.h>

/** Shared VST3 folder list used by the scan dialog and the setup wizard. */
class VstFolderPathList : public juce::Component
{
public:
    VstFolderPathList (juce::StringArray defaultsIn, juce::StringArray extrasIn);

    juce::StringArray getExtras() const { return extras; }
    void browseAndAdd();
    void removeSelected();
    void resized() override;

private:
    void rebuildList();

    class PathList : public juce::ListBox, private juce::ListBoxModel
    {
    public:
        PathList();

        void clear();
        void addItem (juce::String text, bool canSelect);
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
        void selectedRowsChanged (int) override;

    private:
        juce::StringArray items;
        juce::Array<bool> selectable;
    };

    PathList list;
    juce::StringArray defaults, extras;
};
