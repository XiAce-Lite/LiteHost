#pragma once

#include "VstFolderPathList.h"

class ScanFolderPanel : public juce::Component
{
public:
    std::function<void()> onScan;
    std::function<void()> onClose;

    ScanFolderPanel (juce::StringArray defaultsIn, juce::StringArray extrasIn);

    juce::StringArray getExtras() const { return list.getExtras(); }
    void resized() override;

private:
    juce::Label title, hint;
    VstFolderPathList list;
    juce::TextButton addFolder, removeFolder, scan, cancel;
};
