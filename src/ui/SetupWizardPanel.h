#pragma once

#include "VstFolderPathList.h"

class SetupWizardPanel : public juce::Component
{
public:
    std::function<void()> onFinished;
    std::function<void()> onAbandoned;
    std::function<void (juce::StringArray extras, bool startScan)> onCommitVstFolders;
    std::function<bool()> onTryInsertSyncRoom;

    SetupWizardPanel (juce::AudioDeviceManager& devices,
                      juce::StringArray defaultsIn,
                      juce::StringArray extrasIn);
    ~SetupWizardPanel() override;

    void resized() override;

private:
    void finishWizard();
    void leaveVstPage (bool startScan);
    void goTo (int newPage);

    int page = 0;
    bool wantSyncRoom = true;
    bool finished = false;
    juce::Label stepTitle, stepBody, progress;
    juce::AudioDeviceSelectorComponent deviceSelector;
    VstFolderPathList folderList;
    juce::TextButton addFolder, removeFolder, scanNow, insertSyncRoom, back, next, skip;
    juce::ToggleButton wantSyncRoomToggle;
};
