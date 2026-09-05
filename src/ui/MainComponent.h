#pragma once

#include "LookAndFeel.h"
#include "Utf8.h"
#include "MixerStrips.h"
#include "audio/AudioEngine.h"
#include "control/ControlSurface.h"
#include "control/MidiLearn.h"

class PluginEditorWindow;
class PluginScanThread;

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::ChangeListener,
                      public juce::Timer,
                      public ControlSurfaceListener,
                      public MidiLearnListener
{
public:
    explicit MainComponent (juce::String projectPathToOpen = {});
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    juce::File getAppDir() const;
    void setScanStatus (const juce::String& text);
    void scanFinished();

    void promptAddPlugin (const juce::Uuid& trackId, bool master);
    void openPluginEditor (juce::AudioPluginInstance& plugin);
    void removePluginFromTrack (const juce::Uuid& trackId, int index);
    void removePluginFromMaster (int index);
    void closeEditorsFor (juce::AudioPluginInstance* plugin);
    void removeTrack (const juce::Uuid& id);
    void runSyncRoomLoadTest();
    void showLearnMenuForTrack (int trackIndex, MidiLearnTarget target);
    void syncTrackMidiInputs();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    AudioEngine& getEngine() noexcept { return engine; }
    int indexOfTrack (const TrackProcessor& track) const;

    // ControlSurfaceListener / MidiLearnListener shared
    int getNumTracks() const override;
    float getTrackGain (int trackIndex) const override;
    float getTrackPan (int trackIndex) const override;
    bool getTrackMute (int trackIndex) const override;
    bool getTrackSolo (int trackIndex) const override;
    float getMasterGain() const override;
    bool isAudioEngineRunning() const override;
    void setTrackGain (int trackIndex, float gainLinear) override;
    void setTrackPan (int trackIndex, float pan) override;
    void setTrackMute (int trackIndex, bool mute) override;
    void setTrackSolo (int trackIndex, bool solo) override;
    void setMasterGain (float gainLinear) override;
    void setAudioEngineRunning (bool shouldRun) override;
    void controlSurfaceBankChanged (int bankOffset) override;

    // MidiLearnListener
    void setTrackTrim (int trackIndex, float gainLinear) override;
    void midiLearnFinished (bool assigned) override;

private:
    enum MenuIds
    {
        menuNew = 1,
        menuOpen,
        menuSave,
        menuSaveAs,
        menuRecentBase = 100,
        menuRecentClear = 199,
        menuQuit = 200,
        menuSetupWizard = 210
    };

    void setupAudio();
    void applyFirstRunAudioDefaults();
    void showAudioSettings();
    void showSetupWizard (bool allowStarterTrackAutoCreate = false);
    void showSurfaceSettings();
    void showMidiLearnSettings();
    void updateEngineButton();
    void showScanDialog();
    void markSetupWizardCompleted();
    bool findSyncRoomPluginDescription (juce::PluginDescription& out) const;
    bool masterHasSyncRoom() const;
    bool maybeAutoCreateStarterMonoTrack();
    void finishSetupWizardSession (bool allowStarterTrackAutoCreate);
    void startPluginScan (const juce::FileSearchPath& paths);
    void saveAll();
    void savePluginList();
    void loadPluginList();
    void loadAppSettings();
    void saveAppSettings();
    void rememberProject (const juce::File& file);
    bool loadProjectFile (const juce::File& file);
    bool saveProjectFile (const juce::File& file);
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void openRecentProject (int index);
    void clearProjectState();
    void ensureDefaultTrack();
    void updateWindowTitle();
    void rebuildStrips();
    void attachPlugin (const juce::PluginDescription& description, const juce::Uuid& trackId, bool master);
    void armPluginAfterLaunch (juce::AudioPluginInstance* plugin);
    bool isPluginStillLoaded (juce::AudioPluginInstance* plugin) const;
    void refreshAddPluginButtons();
    juce::String makeStatusText() const;
    juce::FileSearchPath defaultVst3ScanPaths() const;
    juce::FileSearchPath buildScanPaths() const;
    juce::File getDefaultProjectsDir() const;
    TrackProcessor* trackAt (int index) const;
    void syncStripsFromEngine();

    LiteLookAndFeel lookAndFeel;
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    AudioEngine engine;
    ControlSurfaceManager controlSurface;
    MidiLearnManager midiLearn;
    bool audioEngineRunning = true;

    juce::MenuBarComponent menuBar;
    juce::Label title;
    juce::Label status;
    juce::TextButton audioButton { jp (u8"オーディオ設定") };
    juce::TextButton engineButton { jp (u8"オーディオ停止") };
    juce::TextButton surfaceButton { jp (u8"サーフェス") };
    juce::TextButton learnButton { jp (u8"MIDI学習") };
    juce::TextButton scanButton { jp (u8"VST3 スキャン") };
    juce::TextButton addTrackButton { jp (u8"トラック追加") };

    juce::Viewport trackViewport;
    juce::Component trackList;
    std::vector<std::unique_ptr<TrackStrip>> strips;
    std::unique_ptr<MasterStrip> masterStrip;
    juce::OwnedArray<PluginEditorWindow> editorWindows;
    std::unique_ptr<PluginScanThread> scanThread;

    juce::String scanStatus;
    int trackSerial = 1;
    juce::File currentProject;
    juce::Array<juce::File> recentProjects;
    juce::StringArray extraVstPaths;
    juce::String startupProjectPath;
    bool setupWizardCompleted = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
