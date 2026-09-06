#include "MainComponent.h"
#include "CrashLog.h"
#include "Utf8.h"
#include "PluginEditorWindow.h"
#include "PluginScanThread.h"
#include "ScanFolderPanel.h"
#include "SetupWizardPanel.h"
#include "SettingsDialogs.h"
#include "UpdateChecker.h"
#include "StartupSplash.h"
#include "app/AppPaths.h"
#include "app/PluginScanCoordinator.h"
#include "app/ProjectStore.h"
#include "audio/SyncRoomFinder.h"
#include <cstdlib>
#include <map>

namespace
{
    constexpr int preferredBufferSize = 128;

    int nearestBufferSize (const juce::Array<int>& sizes, int preferred)
    {
        if (sizes.isEmpty())
            return preferred;

        int best = sizes.getUnchecked (0);
        int bestDist = std::abs (best - preferred);

        for (auto size : sizes)
        {
            const int dist = std::abs (size - preferred);
            if (dist < bestDist || (dist == bestDist && size < best))
            {
                best = size;
                bestDist = dist;
            }
        }

        return best;
    }
}

MainComponent::MainComponent (juce::String projectPathToOpen, StartupProgress* progress)
    : menuBar (this),
      startupProgress (progress),
      startupProjectPath (std::move (projectPathToOpen)),
      mixer (engine, *this)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setSize (980, 700);

    juce::addDefaultFormatsToManager (formatManager);
    reportStartup (jp (u8"設定を読み込み中..."), 0.08);
    loadAppSettings();
    reportStartup (jp (u8"プラグイン一覧を読み込み中..."), 0.16);
    loadPluginList();

    addAndMakeVisible (menuBar);

    title.setText ("LiteHost", juce::dontSendNotification);
    title.setFont (LiteLookAndFeel::uiFont (22.0f, juce::Font::bold));
    addAndMakeVisible (title);
    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));

    audioButton.onClick = [this] { showAudioSettings(); };
    engineButton.onClick = [this] { setAudioEngineRunning (! audioEngineRunning); };
    surfaceButton.onClick = [this] { showSurfaceSettings(); };
    learnButton.onClick = [this] { showMidiLearnSettings(); };
    scanButton.onClick = [this] { showScanDialog(); };
    exclusiveSoloButton.setClickingTogglesState (true);
    exclusiveSoloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LiteLookAndFeel::solo));
    exclusiveSoloButton.setTooltip (jp (u8"Exclusive Solo（Cakewalk）\nON: ソロは1本だけ。次に S を押したトラック以外は解除。\nShift+S の Override は残る。OFF にした瞬間は今のソロを変えない。"));
    exclusiveSoloButton.setToggleState (mixer.getExclusiveSoloMode(), juce::dontSendNotification);
    exclusiveSoloButton.onClick = [this] {
        mixer.setExclusiveSoloMode (exclusiveSoloButton.getToggleState());
        saveAppSettings();
        status.setText (makeStatusText(), juce::dontSendNotification);
    };
    addTrackButton.onClick = [this] {
        {
            const juce::ScopedLock sl (engine.getCallbackLock());
            engine.addTrack (jp (u8"トラック ") + juce::String (trackSerial++));
        }
        rebuildStrips();
        controlSurface.refreshFeedback();
    };

    addAndMakeVisible (audioButton);
    addAndMakeVisible (engineButton);
    addAndMakeVisible (surfaceButton);
    addAndMakeVisible (learnButton);
    addAndMakeVisible (scanButton);
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (exclusiveSoloButton);

    trackViewport.setViewedComponent (&trackList, false);
    trackViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (trackViewport);

    masterStrip = std::make_unique<MasterStrip> (*this);
    addAndMakeVisible (*masterStrip);

    reportStartup (jp (u8"オーディオを開始しています..."), 0.24);
    setupAudio();

    controlSurface.setListener (&mixer);
    controlSurface.setDeviceManager (&deviceManager);
    controlSurface.applySettings();
    midiLearn.setListener (&mixer);
    midiLearn.setDeviceManager (&deviceManager);
    midiLearn.applySettings();
    updateEngineButton();

    CrashLog::write ("ui ready, opening project");
    reportStartup (jp (u8"プロジェクトを開いています..."), 0.30);

    bool opened = false;
    if (startupProjectPath.isNotEmpty())
    {
        const juce::File requested (startupProjectPath);
        if (requested.existsAsFile())
            opened = loadProjectFile (requested);
        else
            status.setText (jp (u8"指定のプロジェクトを開けませんでした: ") + startupProjectPath,
                            juce::dontSendNotification);
    }

    if (! opened && appSettings.currentProject.existsAsFile())
        opened = loadProjectFile (appSettings.currentProject);

    if (! opened)
    {
        const auto legacy = AppPaths::legacySessionFile();
        if (legacy.existsAsFile())
            opened = loadProjectFile (legacy);
    }

    // 初回ウィザード前は空のまま。ウィザード完了時に ASIO+入力があればモノラル1ch で自動作成する。
    if (! opened && appSettings.setupWizardCompleted)
        ensureDefaultTrack();

    CrashLog::write ("project opened=" + juce::String ((int) opened));
    rebuildStrips();
    updateWindowTitle();
    startTimerHz (20);
    CrashLog::write ("MainComponent ctor done");
    reportStartup (jp (u8"準備完了"), 1.0);
    startupProgress = nullptr;

    updateChecker = std::make_unique<UpdateChecker>();
    juce::Timer::callAfterDelay (2500, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->startUpdateCheck();
    });

    if (! appSettings.setupWizardCompleted)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->showSetupWizard (true);
        });
    }
}

MainComponent::~MainComponent()
{
    stopTimer();

    if (scanThread != nullptr)
    {
        scanThread->stopThread (15000);
        scanThread.reset();
    }

    editorWindows.clear();
    captureWindowState();
    saveAll();
    controlSurface.setEnabled (false);
    midiLearn.setEnabled (false);
    deviceManager.removeMidiInputDeviceCallback ({}, &engine);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (LiteLookAndFeel::bg));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (24));
    r = r.reduced (16);

    auto header = r.removeFromTop (44);
    title.setBounds (header.removeFromLeft (110));
    addTrackButton.setBounds (header.removeFromRight (100).reduced (2, 6));
    exclusiveSoloButton.setBounds (header.removeFromRight (86).reduced (2, 6));
    scanButton.setBounds (header.removeFromRight (110).reduced (2, 6));
    learnButton.setBounds (header.removeFromRight (90).reduced (2, 6));
    surfaceButton.setBounds (header.removeFromRight (90).reduced (2, 6));
    engineButton.setBounds (header.removeFromRight (100).reduced (2, 6));
    audioButton.setBounds (header.removeFromRight (110).reduced (2, 6));
    status.setBounds (header.reduced (4, 0));

    if (masterStrip == nullptr)
        return;

    auto master = r.removeFromBottom (210);
    masterStrip->setBounds (master);
    r.removeFromBottom (8);
    trackViewport.setBounds (r);

    const int stripW = TrackStrip::stripWidth;
    const int contentW = juce::jmax (trackViewport.getMaximumVisibleWidth(),
                                     (int) strips.size() * stripW);
    trackList.setBounds (0, 0, contentW, trackViewport.getHeight());

    auto list = trackList.getLocalBounds();
    for (auto& strip : strips)
        strip->setBounds (list.removeFromLeft (stripW));
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { jp (u8"ファイル"), jp (u8"ヘルプ") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 1)
    {
        menu.addItem (menuSetupWizard, jp (u8"セットアップウィザード..."));
        return menu;
    }

    menu.addItem (menuNew, jp (u8"新規プロジェクト"));
    menu.addItem (menuOpen, jp (u8"開く..."));
    menu.addSeparator();
    menu.addItem (menuSave, jp (u8"保存"), appSettings.currentProject != juce::File());
    menu.addItem (menuSaveAs, jp (u8"名前を付けて保存..."));
    menu.addSeparator();

    juce::PopupMenu recent;
    for (int i = 0; i < appSettings.recentProjects.size(); ++i)
        recent.addItem (menuRecentBase + i, appSettings.recentProjects.getReference (i).getFullPathName());
    if (appSettings.recentProjects.isEmpty())
    {
        recent.addItem (-1, jp (u8"（なし）"), false);
    }
    else
    {
        recent.addSeparator();
        recent.addItem (menuRecentClear, jp (u8"履歴をクリア"));
    }
    menu.addSubMenu (jp (u8"最近使ったプロジェクト"), recent);
    menu.addSeparator();
    menu.addItem (menuQuit, jp (u8"終了"));
    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == menuNew)
        newProject();
    else if (menuItemID == menuOpen)
        openProject();
    else if (menuItemID == menuSave)
        saveProject();
    else if (menuItemID == menuSaveAs)
        saveProjectAs();
    else if (menuItemID == menuRecentClear)
    {
        appSettings.clearRecent();
        saveAppSettings();
        menuItemsChanged();
    }
    else if (menuItemID == menuSetupWizard)
        showSetupWizard (false);
    else if (menuItemID == menuQuit)
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }
    else if (menuItemID >= menuRecentBase && menuItemID < menuRecentClear)
        openRecentProject (menuItemID - menuRecentBase);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& strip : strips)
        strip->refreshInputs();

    syncTrackMidiInputs();
    status.setText (makeStatusText(), juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    for (auto& strip : strips)
        if (auto* track = engine.findTrack (strip->getTrackId()))
        {
            strip->setPeak (track->peak.load());
            track->peak.store (track->peak.load() * 0.72f);
        }

    if (masterStrip != nullptr)
    {
        masterStrip->setPeak (engine.masterPeak.load());
        engine.masterPeak.store (engine.masterPeak.load() * 0.72f);
    }

    // Status (~4 Hz) — CPU peak meter is slow-decay; no need to rewrite the label 20×/s.
    static int statusDiv = 0;
    if ((++statusDiv % 5) == 0 && scanStatus.isEmpty())
        status.setText (makeStatusText(), juce::dontSendNotification);

    // Occasional LED / fader mirror to the surface.
    static int feedbackDiv = 0;
    if ((++feedbackDiv % 10) == 0)
        controlSurface.refreshFeedback();
}

juce::File MainComponent::getDefaultProjectsDir() const
{
    return AppPaths::defaultProjectsDirectory();
}

void MainComponent::setScanStatus (const juce::String& text)
{
    scanStatus = text;
    status.setText (text, juce::dontSendNotification);
}

void MainComponent::scanFinished()
{
    savePluginList();
    const auto count = knownPlugins.getNumTypes();
    scanStatus.clear();
    status.setText (jp (u8"VST3 ") + juce::String (count) + jp (u8" 個を登録しました"), juce::dontSendNotification);
    scanButton.setEnabled (true);

    if (scanThread != nullptr)
    {
        scanThread->stopThread (1000);
        scanThread.reset();
    }
}

void MainComponent::applyFirstRunAudioDefaults()
{
    const auto& types = deviceManager.getAvailableDeviceTypes();
    const juce::String preferred =
       #if JUCE_WINDOWS
        "ASIO";
       #elif JUCE_MAC
        "CoreAudio";
       #else
        {};
       #endif

    if (preferred.isNotEmpty())
        for (auto* type : types)
            if (type != nullptr && type->getTypeName().containsIgnoreCase (preferred))
            {
                deviceManager.setCurrentAudioDeviceType (type->getTypeName(), true);
                break;
            }

    auto setup = deviceManager.getAudioDeviceSetup();
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        setup.bufferSize = nearestBufferSize (device->getAvailableBufferSizes(), preferredBufferSize);
        deviceManager.setAudioDeviceSetup (setup, true);
    }
    else
    {
        setup.bufferSize = preferredBufferSize;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

void MainComponent::setupAudio()
{
    deviceManager.addChangeListener (this);
    const auto audioFile = AppPaths::audioFile();
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (audioFile));
    const bool firstRun = xml == nullptr;

    const auto error = deviceManager.initialise (32, 2, xml.get(), true);
    if (error.isNotEmpty())
        status.setText (error, juce::dontSendNotification);

    if (firstRun)
        applyFirstRunAudioDefaults();

    deviceManager.addAudioCallback (&engine);
    deviceManager.addMidiInputDeviceCallback ({}, &engine);
    syncTrackMidiInputs();
}

void MainComponent::syncTrackMidiInputs()
{
    for (auto& track : engine.tracks())
    {
        if (track->midiDeviceId.isEmpty())
            continue;

        if (track->midiDeviceId == AudioEngine::midiAllDevicesId)
        {
            for (const auto& device : juce::MidiInput::getAvailableDevices())
                deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
        }
        else
        {
            deviceManager.setMidiInputDeviceEnabled (track->midiDeviceId, true);
            if (! deviceManager.isMidiInputDeviceEnabled (track->midiDeviceId))
                status.setText (jp (u8"MIDI 入力を開けませんでした: ") + track->midiDeviceId,
                                juce::dontSendNotification);
        }
    }

    // Re-register after enabling so the empty-identifier callback stays active.
    deviceManager.removeMidiInputDeviceCallback ({}, &engine);
    deviceManager.addMidiInputDeviceCallback ({}, &engine);
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 0, 64, 2, 8, true, false, true, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = jp (u8"オーディオ / MIDI 設定");
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::markSetupWizardCompleted()
{
    appSettings.setupWizardCompleted = true;
    saveAppSettings();
}

bool MainComponent::masterHasSyncRoom() const
{
    return SyncRoomFinder::chainContains (engine.masterPlugins());
}

bool MainComponent::findSyncRoomPluginDescription (juce::PluginDescription& out) const
{
    return SyncRoomFinder::findDescription (knownPlugins, formatManager, buildScanPaths(), out);
}

bool MainComponent::maybeAutoCreateStarterMonoTrack()
{
    if (! engine.tracks().empty())
        return false;

   #if JUCE_WINDOWS
    if (deviceManager.getCurrentAudioDeviceType() != "ASIO")
        return false;
   #elif JUCE_MAC
    if (! deviceManager.getCurrentAudioDeviceType().containsIgnoreCase ("CoreAudio"))
        return false;
   #else
    return false;
   #endif

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return false;

    const auto active = device->getActiveInputChannels();
    const auto setup = deviceManager.getAudioDeviceSetup();

    // 有効な入力のうち序数がいちばん若いチャンネルをモノラルで選ぶ（IN1/IN2 なら IN1）。
    int firstMono = -1;
    const int limit = juce::jmax (active.getHighestBit() + 1, setup.inputChannels.getHighestBit() + 1);
    for (int ch = 0; ch < limit; ++ch)
    {
        if (active[ch] && setup.inputChannels[ch])
        {
            firstMono = ch;
            break;
        }
    }

    if (firstMono < 0)
        return false;

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        if (auto* track = engine.addTrack (jp (u8"トラック ") + juce::String (trackSerial++)))
        {
            track->inputStart = firstMono;
            track->inputCount = 1;
            track->midiDeviceId.clear();
        }
    }

    rebuildStrips();
    return true;
}

void MainComponent::finishSetupWizardSession (bool allowStarterTrackAutoCreate)
{
    if (allowStarterTrackAutoCreate)
        maybeAutoCreateStarterMonoTrack();

    if (engine.tracks().empty())
        ensureDefaultTrack();

    rebuildStrips();
    syncTrackMidiInputs();
}

void MainComponent::showSetupWizard (bool allowStarterTrackAutoCreate)
{
    juce::StringArray defaults;
    const auto path = defaultVst3ScanPaths();
    for (int p = 0; p < path.getNumPaths(); ++p)
        defaults.add (path[p].getFullPathName());

    auto panel = std::make_unique<SetupWizardPanel> (deviceManager, defaults, appSettings.extraVstPaths);
    auto* panelPtr = panel.get();
    panel->setSize (640, 580);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = jp (u8"セットアップウィザード");
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    auto* window = options.launchAsync();
    panelPtr->onFinished = [this, window, allowStarterTrackAutoCreate] {
        markSetupWizardCompleted();
        if (auto xml = deviceManager.createStateXml())
            xml->writeTo (AppPaths::audioFile());
        finishSetupWizardSession (allowStarterTrackAutoCreate);
        status.setText (makeStatusText(), juce::dontSendNotification);
        if (window != nullptr)
            window->exitModalState (1);
    };
    panelPtr->onAbandoned = [this] {
        if (engine.tracks().empty())
        {
            ensureDefaultTrack();
            rebuildStrips();
        }
    };
    panelPtr->onCommitVstFolders = [this] (juce::StringArray extras, bool startScan) {
        appSettings.extraVstPaths = std::move (extras);
        saveAppSettings();
        if (startScan)
            startPluginScan (buildScanPaths());
    };
    panelPtr->onTryInsertSyncRoom = [this]() -> bool {
        if (masterHasSyncRoom())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "LiteHost",
                                                    jp (u8"メインアウトにはすでに SyncRoom が入っています。"));
            return true;
        }

        juce::PluginDescription desc;
        if (! findSyncRoomPluginDescription (desc))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::InfoIcon, "LiteHost",
                jp (u8"指定のフォルダに SyncRoom の VST プラグインが見つかりませんでした。\n"
                    u8"挿さずに続行します。インストール後にメインアウトの + VST から追加できます。"));
            return false;
        }

        attachPlugin (desc, {}, true);
        return true;
    };
}

void MainComponent::updateEngineButton()
{
    engineButton.setButtonText (audioEngineRunning ? jp (u8"オーディオ停止") : jp (u8"オーディオ開始"));
}

void MainComponent::showSurfaceSettings()
{
    auto panel = std::make_unique<SurfaceSettingsPanel> (controlSurface);
    auto* panelPtr = panel.get();
    panel->setSize (520, 300);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = jp (u8"コントロールサーフェス");
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    auto* window = options.launchAsync();
    panelPtr->onClose = [window] {
        if (window != nullptr)
            window->exitModalState (0);
    };
    panelPtr->onOk = [this] {
        saveAppSettings();
        status.setText (makeStatusText(), juce::dontSendNotification);
    };
}

void MainComponent::showMidiLearnSettings()
{
    auto panel = std::make_unique<MidiLearnSettingsPanel> (midiLearn);
    auto* panelPtr = panel.get();
    panel->setSize (520, 280);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = jp (u8"MIDI 学習");
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    auto* window = options.launchAsync();
    panelPtr->onClose = [window] {
        if (window != nullptr)
            window->exitModalState (0);
    };
    panelPtr->onOk = [this] {
        saveAppSettings();
        status.setText (makeStatusText(), juce::dontSendNotification);
    };
}

void MainComponent::showLearnMenuForTrack (int trackIndex, MidiLearnTarget target)
{
    juce::PopupMenu menu;
    menu.addItem (1, jp (u8"MIDI 学習"));
    menu.addItem (2, jp (u8"この割り当てをクリア"));
    menu.addItem (3, jp (u8"学習をキャンセル"), midiLearn.isLearning());

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this, trackIndex, target] (int result) {
                            if (result == 1)
                            {
                                if (! midiLearn.isEnabled() || midiLearn.getInputDeviceIdentifier().isEmpty())
                                {
                                    juce::AlertWindow::showMessageBoxAsync (
                                        juce::AlertWindow::InfoIcon, "LiteHost",
                                        jp (u8"先に「MIDI学習」で入力デバイスを有効にしてください。"));
                                    return;
                                }
                                midiLearn.beginLearn (trackIndex, target);
                                status.setText (jp (u8"MIDI 学習中: コントローラを操作してください..."),
                                                juce::dontSendNotification);
                            }
                            else if (result == 2)
                            {
                                midiLearn.clearBinding (trackIndex, target);
                                saveAppSettings();
                                status.setText (jp (u8"MIDI 割り当てを削除しました"), juce::dontSendNotification);
                            }
                            else if (result == 3)
                            {
                                midiLearn.cancelLearn();
                            }
                        });
}

juce::FileSearchPath MainComponent::defaultVst3ScanPaths() const
{
    return PluginScanCoordinator::defaultPaths (formatManager);
}

juce::FileSearchPath MainComponent::buildScanPaths() const
{
    return PluginScanCoordinator::buildPaths (formatManager, appSettings.extraVstPaths);
}

void MainComponent::showScanDialog()
{
    juce::StringArray defaults;
    const auto path = defaultVst3ScanPaths();
    for (int p = 0; p < path.getNumPaths(); ++p)
        defaults.add (path[p].getFullPathName());

    auto panel = std::make_unique<ScanFolderPanel> (defaults, appSettings.extraVstPaths);
    auto* panelPtr = panel.get();
    panel->setSize (560, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = jp (u8"VST3 スキャン");
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    auto* window = options.launchAsync();
    panelPtr->onClose = [window] {
        if (window != nullptr)
            window->exitModalState (0);
    };
    panelPtr->onScan = [this, panelPtr, window] {
        appSettings.extraVstPaths = panelPtr->getExtras();
        saveAppSettings();
        if (window != nullptr)
            window->exitModalState (1);
        startPluginScan (buildScanPaths());
    };
}

void MainComponent::startPluginScan (const juce::FileSearchPath& paths)
{
    auto* format = PluginScanCoordinator::findVst3Format (formatManager);

    if (format == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                jp (u8"VST3 フォーマットを初期化できませんでした。"));
        return;
    }

    if (scanThread != nullptr && scanThread->isThreadRunning())
        return;

    scanButton.setEnabled (false);
    setScanStatus (jp (u8"VST3 をスキャンしています..."));
    scanThread = std::make_unique<PluginScanThread> (*this, knownPlugins, *format, paths);
    scanThread->startThread();
}

void MainComponent::promptAddPlugin (const juce::Uuid& trackId, bool master)
{
    PluginChain* chain = nullptr;
    if (master)
        chain = &engine.masterPlugins();
    else if (auto* track = engine.findTrack (trackId))
        chain = &track->plugins;

    if (chain == nullptr)
        return;

    if (! chain->canAdd())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "LiteHost",
                                                jp (u8"VST はトラック／メインアウトあたり最大 10 個までです。"));
        return;
    }

    const auto types = knownPlugins.getTypes();
    if (types.isEmpty())
    {
        showScanDialog();
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "LiteHost",
                                                jp (u8"まだプラグイン一覧がありません。スキャンが終わったらもう一度押してください。"));
        return;
    }

    juce::PopupMenu menu;
    juce::Array<juce::PluginDescription> ordered;
    std::map<juce::String, juce::PopupMenu> groups;

    int id = 1;
    for (const auto& type : types)
    {
        ordered.add (type);
        const auto vendor = type.manufacturerName.isNotEmpty() ? type.manufacturerName : juce::String ("Other");
        groups[vendor].addItem (id++, type.name);
    }

    for (auto& [vendor, sub] : groups)
        menu.addSubMenu (vendor, sub);

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this, ordered, trackId, master] (int result) {
                            if (result <= 0 || result > ordered.size())
                                return;
                            attachPlugin (ordered.getReference (result - 1), trackId, master);
                        });
}

PluginChain::PluginLoadResult MainComponent::loadPluginIntoChain (PluginChain& chain,
                                                                  const PluginChain::PluginLoadRequest& request,
                                                                  bool suspendBeforePrepare)
{
    const auto setup = deviceManager.getAudioDeviceSetup();
    const double sr = setup.sampleRate > 0.0 ? setup.sampleRate
                     : (engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0);
    const int bs = setup.bufferSize > 0 ? setup.bufferSize
                 : (engine.getBlockSize() > 0 ? engine.getBlockSize() : 128);

    CrashLog::write ("loadPlugin " + request.description.name
                     + " sr=" + juce::String (sr) + " bs=" + juce::String (bs)
                     + " suspendBefore=" + juce::String ((int) suspendBeforePrepare));

    auto result = chain.loadPlugin (request, formatManager, sr, bs, &engine.getPlayHead(),
                                    suspendBeforePrepare, &engine.getCallbackLock());
    if (result.plugin == nullptr)
        CrashLog::write ("loadPlugin FAILED " + request.description.name + " " + result.error);
    else
        CrashLog::write ("loadPlugin ok " + request.description.name
                         + " outs=" + juce::String (result.plugin->getTotalNumOutputChannels())
                         + " delayed=" + juce::String ((int) result.needsDelayedArm));
    return result;
}

void MainComponent::attachPlugin (const juce::PluginDescription& description, const juce::Uuid& trackId, bool master)
{
    PluginChain* chain = nullptr;
    if (master)
        chain = &engine.masterPlugins();
    else if (auto* track = engine.findTrack (trackId))
        chain = &track->plugins;

    if (chain == nullptr || ! chain->canAdd())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "LiteHost",
                                                jp (u8"VST はトラック／メインアウトあたり最大 10 個までです。"));
        return;
    }

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this),
                                      description, trackId, master] {
        if (safe == nullptr)
            return;

        PluginChain* chain = nullptr;
        if (master)
            chain = &safe->engine.masterPlugins();
        else if (auto* track = safe->engine.findTrack (trackId))
            chain = &track->plugins;

        if (chain == nullptr || ! chain->canAdd())
            return;

        CrashLog::write ("attach begin name=" + description.name
                         + " file=" + description.fileOrIdentifier
                         + " in=" + juce::String (description.numInputChannels)
                         + " out=" + juce::String (description.numOutputChannels));

        safe->setScanStatus (jp (u8"プラグインを読み込み中..."));

        // Stop audio so activation cannot race processBlock / ASIO.
        CrashLog::write ("closeAudioDevice");
        safe->deviceManager.closeAudioDevice();
        CrashLog::write ("audio closed");

        PluginChain::PluginLoadRequest request;
        request.description = description;
        const auto result = safe->loadPluginIntoChain (*chain, request, true);

        if (result.plugin == nullptr)
        {
            safe->deviceManager.restartLastAudioDevice();
            safe->scanStatus.clear();
            const auto message = result.error.isNotEmpty() && result.error != "prepare failed"
                ? result.error
                : jp (u8"プラグインの初期化に失敗しました。");
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, jp (u8"プラグインを開けません"), message);
            return;
        }

        CrashLog::write ("created ch="
                         + juce::String (result.plugin->getTotalNumInputChannels()) + "/"
                         + juce::String (result.plugin->getTotalNumOutputChannels())
                         + " buses=" + juce::String (result.plugin->getBusCount (true))
                         + "/" + juce::String (result.plugin->getBusCount (false))
                         + " midiIn=" + juce::String ((int) result.plugin->acceptsMidi())
                         + " instrument=" + juce::String ((int) description.isInstrument));

        for (int i = 0; i < result.plugin->getBusCount (false); ++i)
            if (auto* bus = result.plugin->getBus (false, i))
                CrashLog::write ("  out[" + juce::String (i) + "] " + bus->getName()
                                 + " ch=" + juce::String (bus->getNumberOfChannels())
                                 + " en=" + juce::String ((int) bus->isEnabled()));

        safe->rebuildStrips();
        CrashLog::write ("rebuildStrips done");

        CrashLog::write ("restartLastAudioDevice");
        safe->deviceManager.restartLastAudioDevice();
        CrashLog::write ("audio restarted");

        safe->scanStatus.clear();
        if (result.needsDelayedArm)
        {
            safe->status.setText (jp (u8"プラグイン起動を待っています..."), juce::dontSendNotification);
            safe->armPluginAfterLaunch (result.plugin);
            CrashLog::write ("attach complete (SyncRoom delayed arm)");
        }
        else
        {
            result.plugin->suspendProcessing (false);
            safe->status.setText (safe->makeStatusText(), juce::dontSendNotification);
            CrashLog::write ("attach complete (active)");
        }
    });
}

void MainComponent::runSyncRoomLoadTest()
{
    CrashLog::write ("runSyncRoomLoadTest");
    juce::PluginDescription desc;
    if (! findSyncRoomPluginDescription (desc))
    {
        CrashLog::write ("SyncRoom plugin not found");
        return;
    }

    juce::Timer::callAfterDelay (1000, [safe = juce::Component::SafePointer<MainComponent> (this), desc] {
        if (safe != nullptr)
            safe->attachPlugin (desc, {}, true);
    });
}

void MainComponent::armPluginAfterLaunch (juce::AudioPluginInstance* plugin)
{
    if (plugin == nullptr)
        return;

    const auto name = plugin->getName();
    const bool needsLaunchDelay = SyncRoomFinder::needsDelayedArm (*plugin);

    auto activate = [safe = juce::Component::SafePointer<MainComponent> (this), plugin] {
        if (safe == nullptr || ! safe->isPluginStillLoaded (plugin))
            return;

        CrashLog::write ("unsuspend plugin");
        plugin->suspendProcessing (false);
        safe->status.setText (safe->makeStatusText(), juce::dontSendNotification);
        CrashLog::write ("plugin active");
    };

    if (needsLaunchDelay)
    {
        CrashLog::write ("arm after launch delay name=" + name);
        juce::Timer::callAfterDelay (3000, activate);
    }
    else
        juce::MessageManager::callAsync (activate);
}

bool MainComponent::isPluginStillLoaded (juce::AudioPluginInstance* plugin) const
{
    if (plugin == nullptr)
        return false;

    for (int i = 0; i < engine.masterPlugins().size(); ++i)
        if (engine.masterPlugins().get (i) == plugin)
            return true;

    for (auto& track : engine.tracks())
        for (int i = 0; i < track->plugins.size(); ++i)
            if (track->plugins.get (i) == plugin)
                return true;

    return false;
}

void MainComponent::openPluginEditor (juce::AudioPluginInstance& plugin)
{
    for (auto* window : editorWindows)
    {
        if (window->getPlugin() == &plugin)
        {
            window->toFront (true);
            return;
        }
    }

    editorWindows.add (new PluginEditorWindow (plugin, [this] (PluginEditorWindow* window) {
        editorWindows.removeObject (window);
    }));
}

void MainComponent::closeEditorsFor (juce::AudioPluginInstance* plugin)
{
    for (int i = editorWindows.size(); --i >= 0;)
        if (editorWindows[i]->getPlugin() == plugin)
            editorWindows.remove (i);
}

void MainComponent::removePluginFromTrack (const juce::Uuid& trackId, int index)
{
    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        if (auto* track = engine.findTrack (trackId))
        {
            closeEditorsFor (track->plugins.get (index));
            track->plugins.remove (index);
        }
    }
    rebuildStrips();
}

void MainComponent::removePluginFromMaster (int index)
{
    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        closeEditorsFor (engine.masterPlugins().get (index));
        engine.masterPlugins().remove (index);
    }
    rebuildStrips();
}

void MainComponent::removeTrack (const juce::Uuid& id)
{
    if (auto* track = engine.findTrack (id))
        for (int i = 0; i < track->plugins.size(); ++i)
            closeEditorsFor (track->plugins.get (i));

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        engine.removeTrack (id);
    }
    rebuildStrips();
}

void MainComponent::beginTrackDrag (TrackStrip& strip)
{
    startDragging (juce::String (TrackStrip::dragType) + ":" + strip.getTrackId().toString(), &strip);
}

void MainComponent::reorderTrack (const juce::Uuid& fromId, const juce::Uuid& targetId, bool placeAfter)
{
    if (fromId == targetId)
        return;

    int from = -1, target = -1;
    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        const auto& tracks = engine.tracks();
        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            if (tracks[(size_t) i]->id == fromId)
                from = i;
            if (tracks[(size_t) i]->id == targetId)
                target = i;
        }

        if (from < 0 || target < 0)
            return;

        int dest = placeAfter ? target + 1 : target;
        if (from < dest)
            --dest;

        if (! engine.moveTrack (from, dest))
            return;

        midiLearn.trackMoved (from, dest);
    }

    rebuildStrips();
}

bool MainComponent::applySavedWindowState (juce::ResizableWindow& window)
{
    if (appSettings.windowState.isEmpty())
        return false;

    return window.restoreWindowStateFromString (appSettings.windowState);
}

void MainComponent::captureWindowState()
{
    if (auto* top = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        appSettings.windowState = top->getWindowStateAsString();
}

void MainComponent::startUpdateCheck()
{
    if (updateChecker == nullptr)
        return;

    const auto current = juce::JUCEApplicationBase::getInstance() != nullptr
                             ? juce::JUCEApplicationBase::getInstance()->getApplicationVersion()
                             : juce::String ("0.1.1");

    updateChecker->start (current, appSettings.skippedReleaseTag,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (UpdateChecker::Result result) {
                              if (safe != nullptr)
                                  safe->showUpdateAvailable (result.version, result.tag, result.htmlUrl);
                          });
}

void MainComponent::showUpdateAvailable (const juce::String& version, const juce::String& tag, const juce::String& url)
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::InfoIcon)
                       .withTitle ("LiteHost")
                       .withMessage (jp (u8"新しいバージョン ") + version
                                     + jp (u8" が GitHub で公開されています。\n今のバージョンより新しいリリースです。"))
                       .withButton (jp (u8"ページを開く"))
                       .withButton (jp (u8"この版を無視"))
                       .withButton (jp (u8"閉じる"));

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this), tag, url] (int result) {
        if (safe == nullptr)
            return;

        if (result == 1)
        {
            const auto page = url.isNotEmpty() ? url
                                               : juce::String ("https://github.com/XiAce-Lite/LiteHost/releases");
            juce::URL (page).launchInDefaultBrowser();
        }
        else if (result == 2)
        {
            safe->appSettings.skippedReleaseTag = tag;
            safe->saveAppSettings();
        }
    });
}

void MainComponent::rebuildStrips()
{
    strips.clear();
    trackList.removeAllChildren();

    for (auto& track : engine.tracks())
    {
        auto strip = std::make_unique<TrackStrip> (*this, *track);
        trackList.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    if (masterStrip != nullptr)
    {
        masterStrip->refreshPlugins();
        masterStrip->syncTogglesFromEngine();
    }

    controlSurface.refreshFeedback();
    resized();
}

void MainComponent::refreshAddPluginButtons()
{
    rebuildStrips();
}

void MainComponent::savePluginList()
{
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (AppPaths::knownPluginsFile());
}

void MainComponent::loadPluginList()
{
    if (auto xml = juce::XmlDocument::parse (AppPaths::knownPluginsFile()))
        knownPlugins.recreateFromXml (*xml);
}

void MainComponent::loadAppSettings()
{
    appSettings.load (engine, controlSurface, midiLearn);
}

void MainComponent::saveAppSettings()
{
    appSettings.save (engine, controlSurface, midiLearn);
}

void MainComponent::rememberProject (const juce::File& file)
{
    appSettings.rememberProject (file);
    if (! file.existsAsFile())
        return;

    saveAppSettings();
    updateWindowTitle();
    menuItemsChanged();
}

void MainComponent::updateWindowTitle()
{
    juce::String titleText = "LiteHost";
    if (appSettings.currentProject != juce::File())
        titleText << " - " << appSettings.currentProject.getFileName();

    if (auto* top = getTopLevelComponent())
        if (auto* window = dynamic_cast<juce::DocumentWindow*> (top))
            window->setName (titleText);
}

void MainComponent::clearProjectState()
{
    editorWindows.clear();

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        engine.clearTracksAndMaster();
    }

    trackSerial = 1;
}

void MainComponent::ensureDefaultTrack()
{
    if (! engine.tracks().empty())
        return;

    const juce::ScopedLock sl (engine.getCallbackLock());
    engine.addTrack (jp (u8"トラック ") + juce::String (trackSerial++));
}

void MainComponent::reportStartup (const juce::String& text, double progress01)
{
    if (startupProgress != nullptr)
        startupProgress->setStatus (text, progress01);
}

bool MainComponent::loadProjectFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || xml->getTagName() != "LITEHOST")
        return false;

    const int pluginTotal = ProjectStore::countPlugins (*xml);
    std::unique_ptr<StartupSplashWindow> ownedSplash;
    StartupProgress* progress = startupProgress;
    if (progress == nullptr && pluginTotal > 0)
    {
        ownedSplash = std::make_unique<StartupSplashWindow>();
        progress = ownedSplash.get();
    }

    if (progress != nullptr)
        progress->setStatus (jp (u8"プロジェクトを開いています: ") + file.getFileName(), 0.30);

    clearProjectState();

    CrashLog::write ("loadProject closeAudioDevice " + file.getFileName());
    deviceManager.closeAudioDevice();

    std::vector<juce::AudioPluginInstance*> delayedArm;
    ProjectStore::loadIntoEngine (*xml, engine, trackSerial, delayedArm,
                                  [this] (PluginChain& chain, const PluginChain::PluginLoadRequest& request, bool suspendBeforePrepare) {
                                      CrashLog::write ("load plugin " + request.description.name);
                                      return loadPluginIntoChain (chain, request, suspendBeforePrepare);
                                  },
                                  progress);

    ensureDefaultTrack();
    syncTrackMidiInputs();
    rememberProject (file);
    rebuildStrips();

    CrashLog::write ("loadProject restart audio delayed=" + juce::String ((int) delayedArm.size()));
    if (progress != nullptr)
        progress->setStatus (jp (u8"オーディオを再開しています..."), 0.94);

    deviceManager.restartLastAudioDevice();

    for (auto* plugin : delayedArm)
        armPluginAfterLaunch (plugin);

    CrashLog::write ("loadProject done " + file.getFileName());
    return true;
}

bool MainComponent::saveProjectFile (const juce::File& file)
{
    if (! ProjectStore::saveToFile (file, engine))
        return false;

    rememberProject (file);
    return true;
}

void MainComponent::newProject()
{
    clearProjectState();
    appSettings.currentProject = {};
    ensureDefaultTrack();
    rebuildStrips();
    saveAppSettings();
    updateWindowTitle();
}

void MainComponent::openProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (jp (u8"プロジェクトを開く"),
                                                        appSettings.currentProject.existsAsFile() ? appSettings.currentProject.getParentDirectory()
                                                                                      : getDefaultProjectsDir(),
                                                        "*.litehost;*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file.existsAsFile() && ! loadProjectFile (file))
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                                          jp (u8"プロジェクトを開けませんでした。"));
                          });
}

void MainComponent::saveProject()
{
    if (appSettings.currentProject == juce::File() || ! appSettings.currentProject.hasWriteAccess())
    {
        saveProjectAs();
        return;
    }

    if (! saveProjectFile (appSettings.currentProject))
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                jp (u8"プロジェクトの保存に失敗しました。"));
}

void MainComponent::saveProjectAs()
{
    auto chooser = std::make_shared<juce::FileChooser> (jp (u8"名前を付けて保存"),
                                                        getDefaultProjectsDir().getChildFile ("Untitled.litehost"),
                                                        "*.litehost");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".litehost"))
                                  file = file.withFileExtension (".litehost");
                              if (! saveProjectFile (file))
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                                          jp (u8"プロジェクトの保存に失敗しました。"));
                          });
}

void MainComponent::openRecentProject (int index)
{
    if (! juce::isPositiveAndBelow (index, appSettings.recentProjects.size()))
        return;

    const auto file = appSettings.recentProjects.getReference (index);
    if (! loadProjectFile (file))
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                jp (u8"プロジェクトを開けませんでした。"));
}

void MainComponent::saveAll()
{
    captureWindowState();

    if (appSettings.currentProject != juce::File())
        saveProjectFile (appSettings.currentProject);
    else
    {
        const auto fallback = getDefaultProjectsDir().getChildFile ("Autosave.litehost");
        saveProjectFile (fallback);
    }

    savePluginList();
    saveAppSettings();

    if (auto xml = deviceManager.createStateXml())
        xml->writeTo (AppPaths::audioFile());
}

juce::String MainComponent::makeStatusText() const
{
    juce::String surfaceBit;
    if (controlSurface.isEnabled())
        surfaceBit = "  |  " + controlSurfaceProtocolName (controlSurface.getProtocol())
                   + " bank " + juce::String (controlSurface.getBankOffset() + 1)
                   + "-" + juce::String (controlSurface.getBankOffset() + ControlSurfaceManager::channelsPerBank);

    if (midiLearn.isEnabled())
        surfaceBit += jp (u8"  |  MIDI学習");
    if (midiLearn.isLearning())
        surfaceBit += jp (u8"(待ち)");

    if (engine.exclusiveSoloMode.load())
        surfaceBit += jp (u8"  |  Exclusive Solo");
    if (engine.hasSoloOverride())
        surfaceBit += jp (u8"  |  Solo Override");

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto sr = device->getCurrentSampleRate();
        const auto bs = device->getCurrentBufferSizeSamples();
        const auto latencyMs = sr > 0.0 ? 1000.0 * (double) (device->getInputLatencyInSamples()
                                                            + device->getOutputLatencyInSamples()
                                                            + bs) / sr
                                        : 0.0;
        return device->getName() + "  |  "
             + juce::String (sr / 1000.0, 1) + " kHz  |  "
             + juce::String (bs) + " samples  |  "
             + juce::String (latencyMs, 1) + " ms  |  CPU "
             + juce::String (engine.getCpuPeakLoad() * 100.0f, 0) + "%"
             + surfaceBit;
    }

    if (! audioEngineRunning)
        return jp (u8"オーディオ停止中") + surfaceBit;

    return jp (u8"オーディオデバイスが開かれていません") + surfaceBit;
}

int MainComponent::indexOfTrack (const TrackProcessor& track) const
{
    return mixer.indexOfTrack (track);
}

void MainComponent::syncStripsFromEngine()
{
    for (auto& strip : strips)
        strip->syncFromTrack();
    if (masterStrip != nullptr)
        masterStrip->syncTogglesFromEngine();
}

void MainComponent::mixerUiChanged()
{
    auto sync = [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->syncStripsFromEngine();
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        sync();
    else
        juce::MessageManager::callAsync (sync);
}

bool MainComponent::isAudioEngineRunning() const
{
    return audioEngineRunning;
}

void MainComponent::applySoloClick (const juce::Uuid& trackId, bool shift)
{
    mixer.applySoloClick (trackId, shift);
    syncStripsFromEngine();
    controlSurface.refreshFeedback();
    status.setText (makeStatusText(), juce::dontSendNotification);
}

void MainComponent::midiLearnFinished (bool assigned)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), assigned] {
        if (safe == nullptr)
            return;
        if (assigned)
        {
            safe->saveAppSettings();
            safe->status.setText (jp (u8"MIDI を割り当てました"), juce::dontSendNotification);
        }
        else
        {
            safe->status.setText (jp (u8"MIDI 学習をキャンセルしました"), juce::dontSendNotification);
        }
    });
}

void MainComponent::setAudioEngineRunning (bool shouldRun)
{
    if (shouldRun == audioEngineRunning)
    {
        updateEngineButton();
        return;
    }

    auto apply = [safe = juce::Component::SafePointer<MainComponent> (this), shouldRun] {
        if (safe == nullptr)
            return;

        if (shouldRun)
        {
            safe->deviceManager.restartLastAudioDevice();
            safe->audioEngineRunning = safe->deviceManager.getCurrentAudioDevice() != nullptr;
            if (! safe->audioEngineRunning)
                safe->status.setText (jp (u8"オーディオの開始に失敗しました"), juce::dontSendNotification);
        }
        else
        {
            safe->deviceManager.closeAudioDevice();
            safe->audioEngineRunning = false;
        }

        safe->updateEngineButton();
        safe->status.setText (safe->makeStatusText(), juce::dontSendNotification);
        safe->controlSurface.refreshFeedback();
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}

void MainComponent::controlSurfaceBankChanged (int)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->status.setText (safe->makeStatusText(), juce::dontSendNotification);
    });
}
