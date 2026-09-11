#pragma once

#include "LookAndFeel.h"
#include "MixerStripHost.h"
#include "app/AppSettingsStore.h"
#include "audio/AudioEngine.h"
#include "control/ControlSurface.h"
#include "control/MidiLearn.h"
#include "session/MixerSession.h"

class PluginEditorWindow;
class PluginScanThread;
class TrackStrip;
class MasterStrip;
class UpdateChecker;
class StartupProgress;

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::MenuBarModel,
                      public juce::ChangeListener,
                      public juce::Timer,
                      public MixerSession::Host,
                      public MixerStripHost
{
public:
    explicit MainComponent (juce::String projectPathToOpen = {}, StartupProgress* progress = nullptr);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    void setScanStatus (const juce::String& text) override;
    void scanFinished (ScanFinishInfo info = {}) override;
    juce::Component* asComponent() noexcept override { return this; }

    void promptAddPlugin (const juce::Uuid& trackId, bool master) override;
    void openPluginEditor (juce::AudioPluginInstance& plugin) override;
    void removePluginFromTrack (const juce::Uuid& trackId, int index) override;
    void removePluginFromMaster (int index) override;
    void closeEditorsFor (juce::AudioPluginInstance* plugin);
    void removeTrack (const juce::Uuid& id) override;
    void runSyncRoomLoadTest();
    void showLearnMenuForTrack (int trackIndex, MidiLearnTarget target) override;
    void syncTrackMidiInputs() override;
    void beginTrackDrag (TrackStrip& strip) override;
    void beginPluginDrag (const juce::Uuid& trackId, int pluginIndex, juce::Component& source) override;
    void beginMasterPluginDrag (int pluginIndex, juce::Component& source) override;
    void transferPlugin (const juce::Uuid& fromTrackId, int pluginIndex,
                         const juce::Uuid& toTrackId, int insertIndex) override;
    void reorderMasterPlugin (int pluginIndex, int insertIndex) override;
    void reorderTrack (const juce::Uuid& fromId, const juce::Uuid& targetId, bool placeAfter) override;
    void applySoloClick (const juce::Uuid& trackId, bool shift) override;
    bool applySavedWindowState (juce::ResizableWindow& window);
    /** Returns false if quit was cancelled or a confirm dialog is already open.
        When confirmQuit is on, may return false immediately and call quit later via the app. */
    bool requestQuit();

    juce::AudioDeviceManager& getDeviceManager() noexcept override { return deviceManager; }
    AudioEngine& getEngine() noexcept override { return engine; }
    MixerSession& getMixer() noexcept override { return mixer; }
    int indexOfTrack (const TrackProcessor& track) const override;

    bool isAudioEngineRunning() const override;
    void setAudioEngineRunning (bool shouldRun) override;
    void controlSurfaceBankChanged (int bankOffset) override;
    void midiLearnFinished (bool assigned) override;
    void mixerUiChanged() override;
    void projectEdited() override;
    void markProjectDirty() override;
    void clearProjectDirty();
    /** If dirty, ask Save / Discard / Cancel. Calls proceed only for Save(success) or Discard. */
    void promptIfProjectDirty (std::function<void()> proceed);

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
        menuSetupWizard = 210,
        menuAudioSettings = 300,
        menuSurfaceSettings,
        menuMidiLearnSettings,
        menuVstScan,
        menuOptionsGeneral
    };

    void setupAudio();
    void applyFirstRunAudioDefaults();
    void showAudioSettings();
    void showSetupWizard (bool allowStarterTrackAutoCreate = false);
    void showSurfaceSettings();
    void showMidiLearnSettings();
    void showOptionsGeneral();
    void updateEngineButton();
    void showScanDialog();
    void markSetupWizardCompleted();
    bool findSyncRoomPluginDescription (juce::PluginDescription& out) const;
    bool masterHasSyncRoom() const;
    bool maybeAutoCreateStarterMonoTrack();
    void finishSetupWizardSession (bool allowStarterTrackAutoCreate);
    void startPluginScan (const juce::FileSearchPath& paths);
    void pruneMissingPlugins();
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
    void saveProjectAsThen (std::function<void()> afterSave);
    void openRecentProject (int index);
    void clearProjectState();
    void ensureDefaultTrack();
    void updateWindowTitle();
    void rebuildStrips();
    void attachPlugin (const juce::PluginDescription& description, const juce::Uuid& trackId, bool master);
    PluginChain::PluginLoadResult loadPluginIntoChain (PluginChain& chain,
                                                       const PluginChain::PluginLoadRequest& request,
                                                       bool suspendBeforePrepare);
    void armPluginAfterLaunch (juce::AudioPluginInstance* plugin);
    bool isPluginStillLoaded (juce::AudioPluginInstance* plugin) const;
    void refreshAddPluginButtons();
    juce::String makeStatusText() const;
    void syncStripsFromEngine();
    void captureWindowState();
    void startUpdateCheck();
    void showUpdateAvailable (const juce::String& version, const juce::String& tag, const juce::String& url);
    void reportStartup (const juce::String& text, double progress01);

    StartupProgress* startupProgress = nullptr;
    LiteLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 450 };
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    AudioEngine engine;
    ControlSurfaceManager controlSurface;
    MidiLearnManager midiLearn;
    AppSettingsStore appSettings;
    MixerSession mixer;
    bool audioEngineRunning = true;
    bool scanInProgress = false;
    bool quitConfirmOpen = false;
    bool quitConfirmed = false;
    bool projectDirty = false;
    bool suppressProjectDirty = false;

    juce::MenuBarComponent menuBar;
    juce::Label title;
    juce::Label status;
    juce::TextButton engineButton;
    juce::TextButton addTrackButton;

    juce::Viewport trackViewport;
    juce::Component trackList;
    std::vector<std::unique_ptr<TrackStrip>> strips;
    std::unique_ptr<MasterStrip> masterStrip;
    juce::OwnedArray<PluginEditorWindow> editorWindows;
    std::unique_ptr<PluginScanThread> scanThread;

    juce::String scanStatus;
    int trackSerial = 1;
    juce::String startupProjectPath;
    std::unique_ptr<UpdateChecker> updateChecker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
