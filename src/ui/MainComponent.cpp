#include "MainComponent.h"
#include "CrashLog.h"
#include "Utf8.h"
#include "PluginEditorWindow.h"
#include "PluginScanThread.h"
#include "ScanFolderPanel.h"
#include "SetupWizardPanel.h"
#include "SettingsDialogs.h"
#include "audio/SyncRoomFinder.h"
#include <cstdlib>
#include <map>

namespace
{
    constexpr int preferredBufferSize = 128;

    juce::File appDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("LiteHost");
        dir.createDirectory();
        return dir;
    }

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

MainComponent::MainComponent (juce::String projectPathToOpen)
    : menuBar (this),
      startupProjectPath (std::move (projectPathToOpen))
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setSize (980, 700);

    juce::addDefaultFormatsToManager (formatManager);
    loadAppSettings();
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

    trackViewport.setViewedComponent (&trackList, false);
    trackViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (trackViewport);

    masterStrip = std::make_unique<MasterStrip> (*this);
    addAndMakeVisible (*masterStrip);

    setupAudio();

    controlSurface.setListener (this);
    controlSurface.setDeviceManager (&deviceManager);
    controlSurface.applySettings();
    midiLearn.setListener (this);
    midiLearn.setDeviceManager (&deviceManager);
    midiLearn.applySettings();
    updateEngineButton();

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

    if (! opened && currentProject.existsAsFile())
        opened = loadProjectFile (currentProject);

    if (! opened)
    {
        const auto legacy = getAppDir().getChildFile ("session.xml");
        if (legacy.existsAsFile())
            opened = loadProjectFile (legacy);
    }

    // 初回ウィザード前は空のまま。ウィザード完了時に ASIO+入力があればモノラル1ch で自動作成する。
    if (! opened && setupWizardCompleted)
        ensureDefaultTrack();

    rebuildStrips();
    updateWindowTitle();
    startTimerHz (20);

    if (! setupWizardCompleted)
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
    scanButton.setBounds (header.removeFromRight (110).reduced (2, 6));
    learnButton.setBounds (header.removeFromRight (90).reduced (2, 6));
    surfaceButton.setBounds (header.removeFromRight (90).reduced (2, 6));
    engineButton.setBounds (header.removeFromRight (100).reduced (2, 6));
    audioButton.setBounds (header.removeFromRight (110).reduced (2, 6));
    status.setBounds (header.reduced (4, 0));

    if (masterStrip == nullptr)
        return;

    auto master = r.removeFromBottom (170);
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
    menu.addItem (menuSave, jp (u8"保存"), currentProject != juce::File());
    menu.addItem (menuSaveAs, jp (u8"名前を付けて保存..."));
    menu.addSeparator();

    juce::PopupMenu recent;
    for (int i = 0; i < recentProjects.size(); ++i)
        recent.addItem (menuRecentBase + i, recentProjects.getReference (i).getFullPathName());
    if (recentProjects.isEmpty())
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
        recentProjects.clear();
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

juce::File MainComponent::getAppDir() const
{
    return appDirectory();
}

juce::File MainComponent::getDefaultProjectsDir() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("LiteHost");
    dir.createDirectory();
    return dir;
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
   #if JUCE_WINDOWS
    const auto& types = deviceManager.getAvailableDeviceTypes();
    bool hasAsio = false;
    for (auto* type : types)
        if (type != nullptr && type->getTypeName() == "ASIO")
            hasAsio = true;

    if (hasAsio)
        deviceManager.setCurrentAudioDeviceType ("ASIO", true);
   #endif

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
    const auto audioFile = getAppDir().getChildFile ("audio.xml");
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
    setupWizardCompleted = true;
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

    auto panel = std::make_unique<SetupWizardPanel> (deviceManager, defaults, extraVstPaths);
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
            xml->writeTo (getAppDir().getChildFile ("audio.xml"));
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
        extraVstPaths = std::move (extras);
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
    // JUCE 既定には %LocalAppData%\Programs\Common\VST3（Users 配下）も含まれるが、
    // 標準スキャンは Common Files\VST3 のみ。ユーザー領域は「フォルダを追加」で指定する。
    juce::FileSearchPath paths;

   #if JUCE_WINDOWS
    paths.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::windowsProgramFilesCommon)
                                    .getChildFile ("VST3"));

    const juce::File x86Common ("C:\\Program Files (x86)\\Common Files\\VST3");
    paths.addIfNotAlreadyThere (x86Common);
   #else
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (auto* format = formatManager.getFormat (i); format != nullptr && format->getName() == "VST3")
        {
            const auto all = format->getDefaultLocationsToSearch();
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            for (int p = 0; p < all.getNumPaths(); ++p)
            {
                const auto& dir = all[p];
                if (! dir.isAChildOf (home) && dir != home)
                    paths.addIfNotAlreadyThere (dir);
            }
            break;
        }
   #endif

    return paths;
}

juce::FileSearchPath MainComponent::buildScanPaths() const
{
    auto paths = defaultVst3ScanPaths();

    for (const auto& extra : extraVstPaths)
        paths.addIfNotAlreadyThere (juce::File (extra));

    return paths;
}

void MainComponent::showScanDialog()
{
    juce::StringArray defaults;
    const auto path = defaultVst3ScanPaths();
    for (int p = 0; p < path.getNumPaths(); ++p)
        defaults.add (path[p].getFullPathName());

    auto panel = std::make_unique<ScanFolderPanel> (defaults, extraVstPaths);
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
        extraVstPaths = panelPtr->getExtras();
        saveAppSettings();
        if (window != nullptr)
            window->exitModalState (1);
        startPluginScan (buildScanPaths());
    };
}

void MainComponent::startPluginScan (const juce::FileSearchPath& paths)
{
    auto* format = formatManager.getFormat (0);
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (formatManager.getFormat (i)->getName() == "VST3")
            format = formatManager.getFormat (i);

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

        const auto setup = safe->deviceManager.getAudioDeviceSetup();
        const double useSr = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
        const int useBs = setup.bufferSize > 0 ? setup.bufferSize : 128;

        CrashLog::write ("createPluginInstance sr=" + juce::String (useSr) + " bs=" + juce::String (useBs));
        juce::String error;
        auto instance = PluginChain::instantiate (description, safe->formatManager, useSr, useBs, error);
        if (instance == nullptr)
        {
            CrashLog::write ("create FAILED: " + error);
            safe->deviceManager.restartLastAudioDevice();
            safe->scanStatus.clear();
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, jp (u8"プラグインを開けません"), error);
            return;
        }

        CrashLog::write ("created ch="
                         + juce::String (instance->getTotalNumInputChannels()) + "/"
                         + juce::String (instance->getTotalNumOutputChannels())
                         + " buses=" + juce::String (instance->getBusCount (true))
                         + "/" + juce::String (instance->getBusCount (false))
                         + " midiIn=" + juce::String ((int) instance->acceptsMidi())
                         + " instrument=" + juce::String ((int) description.isInstrument));

        for (int i = 0; i < instance->getBusCount (false); ++i)
            if (auto* bus = instance->getBus (false, i))
                CrashLog::write ("  out[" + juce::String (i) + "] " + bus->getName()
                                 + " ch=" + juce::String (bus->getNumberOfChannels())
                                 + " en=" + juce::String ((int) bus->isEnabled()));

        const bool needsLaunchDelay = description.name.containsIgnoreCase ("SyncRoom")
                                   || description.name.containsIgnoreCase ("syncroom");

        instance->suspendProcessing (true);
        chain->setPlayHead (&safe->engine.getPlayHead());

        CrashLog::write ("prepareInstance");
        if (! PluginChain::prepareInstance (*instance, useSr, useBs, false, &safe->engine.getPlayHead()))
        {
            CrashLog::write ("prepare FAILED");
            safe->deviceManager.restartLastAudioDevice();
            safe->scanStatus.clear();
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, jp (u8"プラグインを開けません"),
                                                    jp (u8"プラグインの初期化に失敗しました。"));
            return;
        }
        CrashLog::write ("prepare ok outs=" + juce::String (instance->getTotalNumOutputChannels()));

        juce::AudioPluginInstance* added = nullptr;
        {
            const juce::ScopedLock sl (safe->engine.getCallbackLock());
            added = chain->addPrepared (std::move (instance));
            if (added != nullptr && ! needsLaunchDelay)
                added->suspendProcessing (false);
        }

        if (added == nullptr)
        {
            CrashLog::write ("add FAILED");
            safe->deviceManager.restartLastAudioDevice();
            safe->scanStatus.clear();
            return;
        }
        CrashLog::write ("added to chain suspended=" + juce::String ((int) added->isSuspended()));

        safe->rebuildStrips();
        CrashLog::write ("rebuildStrips done");

        CrashLog::write ("restartLastAudioDevice");
        safe->deviceManager.restartLastAudioDevice();
        CrashLog::write ("audio restarted");

        safe->scanStatus.clear();
        if (needsLaunchDelay)
        {
            safe->status.setText (jp (u8"プラグイン起動を待っています..."), juce::dontSendNotification);
            safe->armPluginAfterLaunch (added);
            CrashLog::write ("attach complete (SyncRoom delayed arm)");
        }
        else
        {
            added->suspendProcessing (false);
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
    const bool needsLaunchDelay = name.containsIgnoreCase ("SyncRoom")
                               || name.containsIgnoreCase ("syncroom");

    auto activate = [safe = juce::Component::SafePointer<MainComponent> (this), plugin] {
        if (safe == nullptr || ! safe->isPluginStillLoaded (plugin))
            return;

        CrashLog::write ("unsuspend plugin");
        plugin->suspendProcessing (false);
        safe->status.setText (safe->makeStatusText(), juce::dontSendNotification);
        CrashLog::write ("plugin active");
    };

    if (needsLaunchDelay)
        juce::Timer::callAfterDelay (3000, activate);
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
        xml->writeTo (getAppDir().getChildFile ("knownPlugins.xml"));
}

void MainComponent::loadPluginList()
{
    if (auto xml = juce::XmlDocument::parse (getAppDir().getChildFile ("knownPlugins.xml")))
        knownPlugins.recreateFromXml (*xml);
}

void MainComponent::loadAppSettings()
{
    recentProjects.clear();
    extraVstPaths.clear();
    currentProject = {};
    setupWizardCompleted = false;

    auto xml = juce::XmlDocument::parse (getAppDir().getChildFile ("settings.xml"));
    if (xml == nullptr || xml->getTagName() != "SETTINGS")
        return;

    // 既存ユーザーはフラグ無しでも完了済み扱い（アップグレードで突然出さない）
    setupWizardCompleted = xml->getBoolAttribute ("setupWizardCompleted", true);

    currentProject = juce::File (xml->getStringAttribute ("lastProject"));

    if (auto* recent = xml->getChildByName ("RECENT"))
        for (auto* child = recent->getFirstChildElement(); child != nullptr; child = child->getNextElement())
            if (child->getTagName() == "PROJECT")
            {
                const juce::File file (child->getStringAttribute ("path"));
                if (file.existsAsFile())
                    recentProjects.addIfNotAlreadyThere (file);
            }

    if (auto* paths = xml->getChildByName ("VST_PATHS"))
        for (auto* child = paths->getFirstChildElement(); child != nullptr; child = child->getNextElement())
            if (child->getTagName() == "PATH")
                extraVstPaths.addIfNotAlreadyThere (child->getStringAttribute ("value"));

    controlSurface.readXml (*xml);
    midiLearn.readXml (*xml);
}

void MainComponent::saveAppSettings()
{
    juce::XmlElement xml ("SETTINGS");
    xml.setAttribute ("lastProject", currentProject.getFullPathName());
    xml.setAttribute ("setupWizardCompleted", setupWizardCompleted ? 1 : 0);

    auto* recent = xml.createNewChildElement ("RECENT");
    for (const auto& file : recentProjects)
    {
        auto* child = recent->createNewChildElement ("PROJECT");
        child->setAttribute ("path", file.getFullPathName());
    }

    auto* paths = xml.createNewChildElement ("VST_PATHS");
    for (const auto& path : extraVstPaths)
    {
        auto* child = paths->createNewChildElement ("PATH");
        child->setAttribute ("value", path);
    }

    controlSurface.writeXml (xml);
    midiLearn.writeXml (xml);
    xml.writeTo (getAppDir().getChildFile ("settings.xml"));
}

void MainComponent::rememberProject (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    currentProject = file;
    recentProjects.removeAllInstancesOf (file);
    recentProjects.insert (0, file);

    while (recentProjects.size() > 10)
        recentProjects.removeLast();

    saveAppSettings();
    updateWindowTitle();
    menuItemsChanged();
}

void MainComponent::updateWindowTitle()
{
    juce::String titleText = "LiteHost";
    if (currentProject != juce::File())
        titleText << " - " << currentProject.getFileName();

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

bool MainComponent::loadProjectFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || xml->getTagName() != "LITEHOST")
        return false;

    clearProjectState();

    auto restoreChain = [this] (PluginChain& chain, juce::XmlElement& parent) {
        const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
        const int bs = AudioEngine::maxBlockSize;

        for (auto* child = parent.getFirstChildElement(); child != nullptr; child = child->getNextElement())
        {
            if (child->getTagName() != "PLUGIN")
                continue;
            if (! chain.canAdd())
                break;

            juce::PluginDescription description;
            if (! description.loadFromXml (*child))
                continue;

            juce::String error;
            auto instance = PluginChain::instantiate (description, formatManager, sr, bs, error);
            if (instance == nullptr)
                continue;

            if (child->hasAttribute ("state"))
            {
                juce::MemoryBlock state;
                state.fromBase64Encoding (child->getStringAttribute ("state"));
                instance->setStateInformation (state.getData(), (int) state.getSize());
            }

            if (! PluginChain::prepareInstance (*instance, sr, bs, false, &engine.getPlayHead()))
                continue;

            const bool needsLaunchDelay = description.name.containsIgnoreCase ("SyncRoom")
                                       || description.name.containsIgnoreCase ("syncroom");
            instance->suspendProcessing (needsLaunchDelay);
            if (auto* added = chain.addPrepared (std::move (instance)))
            {
                const int index = chain.size() - 1;
                chain.setBypassed (index, child->getBoolAttribute ("bypass", false));
                if (needsLaunchDelay)
                    armPluginAfterLaunch (added);
            }
        }
    };

    if (auto* master = xml->getChildByName ("MASTER"))
    {
        engine.reverbEnabled = master->getBoolAttribute ("reverb", false);
        engine.limiterEnabled = master->getBoolAttribute ("limiter", false);
        engine.reverbWet = (float) master->getDoubleAttribute ("wet", 0.18);
        engine.reverbRoom = (float) master->getDoubleAttribute ("room", 0.42);
        engine.reverbDamping = (float) master->getDoubleAttribute ("damping", 0.4);
        engine.limiterThresholdDb = (float) master->getDoubleAttribute ("ceiling", -0.3);
        engine.masterGain = (float) master->getDoubleAttribute ("gain", 1.0);
        restoreChain (engine.masterPlugins(), *master);
    }

    if (auto* tracks = xml->getChildByName ("TRACKS"))
    {
        for (auto* child = tracks->getFirstChildElement(); child != nullptr; child = child->getNextElement())
        {
            if (child->getTagName() != "TRACK")
                continue;

            auto* track = engine.addTrack (child->getStringAttribute ("name", jp (u8"トラック")));
            track->inputStart = child->getIntAttribute ("inputStart", 0);
            track->inputCount = child->getIntAttribute ("inputCount", 2);
            track->midiDeviceId = child->getStringAttribute ("midiInput");
            if (track->midiDeviceId.isNotEmpty())
                track->inputStart = -1;
            track->trim = (float) child->getDoubleAttribute ("trim", 1.0);
            track->gain = (float) child->getDoubleAttribute ("gain", 1.0);
            track->pan = (float) child->getDoubleAttribute ("pan", 0.0);
            track->mute = child->getBoolAttribute ("mute", false);
            track->solo = child->getBoolAttribute ("solo", false);
            restoreChain (track->plugins, *child);
            ++trackSerial;
        }
    }

    ensureDefaultTrack();
    syncTrackMidiInputs();
    rememberProject (file);
    rebuildStrips();
    return true;
}

bool MainComponent::saveProjectFile (const juce::File& file)
{
    juce::XmlElement xml ("LITEHOST");
    xml.setAttribute ("version", 1);

    auto* master = xml.createNewChildElement ("MASTER");
    master->setAttribute ("reverb", engine.reverbEnabled.load());
    master->setAttribute ("limiter", engine.limiterEnabled.load());
    master->setAttribute ("wet", engine.reverbWet.load());
    master->setAttribute ("room", engine.reverbRoom.load());
    master->setAttribute ("damping", engine.reverbDamping.load());
    master->setAttribute ("ceiling", engine.limiterThresholdDb.load());
    master->setAttribute ("gain", engine.masterGain.load());

    for (int i = 0; i < engine.masterPlugins().size(); ++i)
    {
        auto descXml = engine.masterPlugins().descriptionAt (i).createXml();
        if (descXml == nullptr)
            continue;

        descXml->setTagName ("PLUGIN");
        if (auto* plugin = engine.masterPlugins().get (i))
        {
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            descXml->setAttribute ("state", state.toBase64Encoding());
        }
        descXml->setAttribute ("bypass", engine.masterPlugins().isBypassed (i) ? 1 : 0);
        master->addChildElement (descXml.release());
    }

    auto* tracks = xml.createNewChildElement ("TRACKS");
    for (auto& track : engine.tracks())
    {
        auto* child = tracks->createNewChildElement ("TRACK");
        child->setAttribute ("name", track->name);
        child->setAttribute ("inputStart", track->inputStart.load());
        child->setAttribute ("inputCount", track->inputCount.load());
        if (track->midiDeviceId.isNotEmpty())
            child->setAttribute ("midiInput", track->midiDeviceId);
        child->setAttribute ("trim", track->trim.load());
        child->setAttribute ("gain", track->gain.load());
        child->setAttribute ("pan", track->pan.load());
        child->setAttribute ("mute", track->mute.load());
        child->setAttribute ("solo", track->solo.load());

        for (int i = 0; i < track->plugins.size(); ++i)
        {
            auto descXml = track->plugins.descriptionAt (i).createXml();
            if (descXml == nullptr)
                continue;

            descXml->setTagName ("PLUGIN");
            if (auto* plugin = track->plugins.get (i))
            {
                juce::MemoryBlock state;
                plugin->getStateInformation (state);
                descXml->setAttribute ("state", state.toBase64Encoding());
            }
            descXml->setAttribute ("bypass", track->plugins.isBypassed (i) ? 1 : 0);
            child->addChildElement (descXml.release());
        }
    }

    if (! xml.writeTo (file))
        return false;

    rememberProject (file);
    return true;
}

void MainComponent::newProject()
{
    clearProjectState();
    currentProject = {};
    ensureDefaultTrack();
    rebuildStrips();
    saveAppSettings();
    updateWindowTitle();
}

void MainComponent::openProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (jp (u8"プロジェクトを開く"),
                                                        currentProject.existsAsFile() ? currentProject.getParentDirectory()
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
    if (currentProject == juce::File() || ! currentProject.hasWriteAccess())
    {
        saveProjectAs();
        return;
    }

    if (! saveProjectFile (currentProject))
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
    if (! juce::isPositiveAndBelow (index, recentProjects.size()))
        return;

    const auto file = recentProjects.getReference (index);
    if (! loadProjectFile (file))
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                jp (u8"プロジェクトを開けませんでした。"));
}

void MainComponent::saveAll()
{
    if (currentProject != juce::File())
        saveProjectFile (currentProject);
    else
    {
        const auto fallback = getDefaultProjectsDir().getChildFile ("Autosave.litehost");
        saveProjectFile (fallback);
    }

    savePluginList();
    saveAppSettings();

    if (auto xml = deviceManager.createStateXml())
        xml->writeTo (getAppDir().getChildFile ("audio.xml"));
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

TrackProcessor* MainComponent::trackAt (int index) const
{
    const juce::ScopedLock sl (engine.getCallbackLock());
    const auto& tracks = engine.tracks();
    if (! juce::isPositiveAndBelow (index, (int) tracks.size()))
        return nullptr;
    return tracks[(size_t) index].get();
}

int MainComponent::indexOfTrack (const TrackProcessor& track) const
{
    const juce::ScopedLock sl (engine.getCallbackLock());
    const auto& tracks = engine.tracks();
    for (int i = 0; i < (int) tracks.size(); ++i)
        if (tracks[(size_t) i].get() == &track)
            return i;
    return -1;
}

void MainComponent::syncStripsFromEngine()
{
    for (auto& strip : strips)
        strip->syncFromTrack();
    if (masterStrip != nullptr)
        masterStrip->syncTogglesFromEngine();
}

int MainComponent::getNumTracks() const
{
    return engine.getTrackCount();
}

float MainComponent::getTrackGain (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->gain.load();
    return 1.0f;
}

float MainComponent::getTrackPan (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->pan.load();
    return 0.0f;
}

bool MainComponent::getTrackMute (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->mute.load();
    return false;
}

bool MainComponent::getTrackSolo (int trackIndex) const
{
    if (auto* track = trackAt (trackIndex))
        return track->solo.load();
    return false;
}

float MainComponent::getMasterGain() const
{
    return engine.masterGain.load();
}

bool MainComponent::isAudioEngineRunning() const
{
    return audioEngineRunning;
}

void MainComponent::setTrackGain (int trackIndex, float gainLinear)
{
    if (auto* track = trackAt (trackIndex))
        track->gain = juce::jlimit (0.0f, juce::Decibels::decibelsToGain (12.0f), gainLinear);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->syncStripsFromEngine();
    });
}

void MainComponent::setTrackTrim (int trackIndex, float gainLinear)
{
    if (auto* track = trackAt (trackIndex))
        track->trim = juce::jlimit (0.0f, juce::Decibels::decibelsToGain (24.0f), gainLinear);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->syncStripsFromEngine();
    });
}

void MainComponent::setTrackPan (int trackIndex, float panValue)
{
    if (auto* track = trackAt (trackIndex))
        track->pan = juce::jlimit (-1.0f, 1.0f, panValue);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->syncStripsFromEngine();
    });
}

void MainComponent::setTrackMute (int trackIndex, bool mute)
{
    if (auto* track = trackAt (trackIndex))
        track->mute = mute;

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->syncStripsFromEngine();
    });
}

void MainComponent::setTrackSolo (int trackIndex, bool solo)
{
    if (auto* track = trackAt (trackIndex))
        track->solo = solo;

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->syncStripsFromEngine();
    });
}

void MainComponent::setMasterGain (float gainLinear)
{
    engine.masterGain = juce::jlimit (0.0f, juce::Decibels::decibelsToGain (12.0f), gainLinear);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe == nullptr || safe->masterStrip == nullptr)
            return;
        safe->masterStrip->syncTogglesFromEngine();
    });
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
