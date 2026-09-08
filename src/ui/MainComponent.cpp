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
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    constexpr int preferredBufferSize = 128;

    /**
     * In-app modal (child of MainComponent). Same HWND as the main window so
     * keyboard focus works; title-bar close button included.
     */
    class AppModalOverlay final : public juce::Component,
                                  private juce::ComponentListener
    {
    public:
        explicit AppModalOverlay (juce::DialogWindow::LaunchOptions& options)
            : titleText (options.dialogTitle),
              panelColour (options.dialogBackgroundColour),
              escapeCloses (options.escapeKeyTriggersCloseButton)
        {
            setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (false);
            getProperties().set ("appModalOverlay", true);

            content.reset (options.content.release());
            jassert (content != nullptr);
            addAndMakeVisible (*content);
            prepareTabStops (*content);

            closeButton.setButtonText ("x");
            closeButton.setTooltip (jp (u8"閉じる"));
            closeButton.setWantsKeyboardFocus (false);
            closeButton.onClick = [this] { exitModalState (0); };
            addAndMakeVisible (closeButton);

            contentSize = { content->getWidth(), content->getHeight() };
            if (contentSize.x <= 0 || contentSize.y <= 0)
                contentSize = { 420, 320 };
        }

        ~AppModalOverlay() override
        {
            if (auto* p = getParentComponent())
                p->removeComponentListener (this);
        }

        void parentHierarchyChanged() override
        {
            if (auto* p = getParentComponent())
            {
                p->addComponentListener (this);
                setBounds (p->getLocalBounds());
            }
        }

        void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
        {
            if (wasResized && &c == getParentComponent())
                setBounds (c.getLocalBounds());
        }

        void componentBeingDeleted (juce::Component& c) override
        {
            if (&c == getParentComponent())
                c.removeComponentListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.55f));

            const auto panel = getPanelBounds().toFloat();
            g.setColour (panelColour);
            g.fillRoundedRectangle (panel, 8.0f);
            g.setColour (juce::Colour (0xff3a4254));
            g.drawRoundedRectangle (panel, 8.0f, 1.0f);

            auto titleArea = getPanelBounds().removeFromTop (titleBarH);
            g.setColour (juce::Colour (LiteLookAndFeel::text));
            g.setFont (LiteLookAndFeel::uiFont (15.0f, juce::Font::bold));
            g.drawText (titleText, titleArea.withTrimmedRight (titleBarH + 8).reduced (14, 0),
                        juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            const auto panel = getPanelBounds();
            closeButton.setBounds (panel.getRight() - titleBarH + 4, panel.getY() + 4,
                                   titleBarH - 8, titleBarH - 8);
            if (content != nullptr)
                content->setBounds (panel.withTrimmedTop (titleBarH));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (escapeCloses && ! getPanelBounds().contains (e.getPosition()))
                exitModalState (0);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.isKeyCode (juce::KeyPress::tabKey))
                return cycleFocus (! key.getModifiers().isShiftDown());

            if (escapeCloses && key.isKeyCode (juce::KeyPress::escapeKey))
            {
                exitModalState (0);
                return true;
            }

            return false;
        }

        bool cycleFocus (bool forward)
        {
            // KEYDOWN + WM_CHAR can deliver Tab twice; ignore the duplicate.
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastTabMs < 80)
                return true;
            lastTabMs = now;

            juce::Array<juce::Component*> focusable;
            collectFocusable (content.get(), focusable);
            if (focusable.isEmpty())
                return false;

            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            int index = -1;
            if (focused != nullptr)
            {
                for (int i = 0; i < focusable.size(); ++i)
                {
                    auto* c = focusable.getUnchecked (i);
                    if (c == focused || c->isParentOf (focused))
                    {
                        index = i;
                        break;
                    }
                }
            }

            const int n = focusable.size();
            const int next = index < 0 ? 0
                                       : (forward ? (index + 1) % n : (index - 1 + n) % n);
            auto* target = focusable.getUnchecked (next);
            target->grabKeyboardFocus();
            target->repaint();
            if (focused != nullptr)
                focused->repaint();
            return true;
        }

    private:
        static constexpr int titleBarH = 40;
        static constexpr int panelPad = 24;

        juce::Rectangle<int> getPanelBounds() const
        {
            const int w = juce::jmin (contentSize.x, juce::jmax (200, getWidth() - panelPad * 2));
            const int h = juce::jmin (contentSize.y + titleBarH,
                                     juce::jmax (160, getHeight() - panelPad * 2));
            return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
        }

        static void prepareTabStops (juce::Component& root)
        {
            for (int i = 0; i < root.getNumChildComponents(); ++i)
            {
                auto* c = root.getChildComponent (i);
                if (c == nullptr)
                    continue;

                if (dynamic_cast<juce::ComboBox*> (c) != nullptr
                    || dynamic_cast<juce::Button*> (c) != nullptr
                    || dynamic_cast<juce::ListBox*> (c) != nullptr
                    || dynamic_cast<juce::TreeView*> (c) != nullptr
                    || dynamic_cast<juce::TextEditor*> (c) != nullptr
                    || dynamic_cast<juce::FilenameComponent*> (c) != nullptr)
                {
                    c->setWantsKeyboardFocus (true);
                }
                else if (auto* slider = dynamic_cast<juce::Slider*> (c))
                {
                    // Prefer the numeric text box for Tab; keep the track mouse-only.
                    slider->setWantsKeyboardFocus (slider->getTextBoxPosition() == juce::Slider::NoTextBox);
                }
                else if (auto* label = dynamic_cast<juce::Label*> (c))
                {
                    const bool sliderTextBox = dynamic_cast<juce::Slider*> (label->getParentComponent()) != nullptr;
                    label->setWantsKeyboardFocus (sliderTextBox || label->isEditable());
                }

                prepareTabStops (*c);
            }
        }

        static bool isTabStop (juce::Component& c)
        {
            if (! c.isVisible() || ! c.isEnabled() || c.getWidth() <= 0 || c.getHeight() <= 0)
                return false;

            if (dynamic_cast<juce::Button*> (&c) != nullptr
                || dynamic_cast<juce::ComboBox*> (&c) != nullptr
                || dynamic_cast<juce::ListBox*> (&c) != nullptr
                || dynamic_cast<juce::TreeView*> (&c) != nullptr
                || dynamic_cast<juce::TextEditor*> (&c) != nullptr
                || dynamic_cast<juce::FilenameComponent*> (&c) != nullptr)
            {
                return c.getWantsKeyboardFocus();
            }

            if (auto* slider = dynamic_cast<juce::Slider*> (&c))
                return slider->getWantsKeyboardFocus()
                    && slider->getTextBoxPosition() == juce::Slider::NoTextBox;

            if (auto* label = dynamic_cast<juce::Label*> (&c))
            {
                if (dynamic_cast<juce::Slider*> (label->getParentComponent()) != nullptr)
                    return true;
                return label->isEditable() && label->getWantsKeyboardFocus();
            }

            return false;
        }

        static void collectFocusableRecursive (juce::Component* root, juce::Array<juce::Component*>& out)
        {
            if (root == nullptr)
                return;

            for (int i = 0; i < root->getNumChildComponents(); ++i)
            {
                auto* c = root->getChildComponent (i);
                if (c == nullptr || ! c->isVisible() || ! c->isEnabled())
                    continue;

                if (isTabStop (*c))
                    out.add (c);

                collectFocusableRecursive (c, out);
            }
        }

        static void collectFocusable (juce::Component* root, juce::Array<juce::Component*>& out)
        {
            collectFocusableRecursive (root, out);

            // Reading order: top → bottom, then left → right (same row ≈ 10px).
            struct RowCol
            {
                juce::Component* c;
                int row;
                int x;
            };

            juce::Array<RowCol> keyed;
            keyed.ensureStorageAllocated (out.size());
            for (auto* c : out)
            {
                const auto screen = c->getScreenBounds();
                keyed.add ({ c, screen.getY(), screen.getX() });
            }

            std::sort (keyed.begin(), keyed.end(), [] (const RowCol& a, const RowCol& b) {
                if (std::abs (a.row - b.row) > 10)
                    return a.row < b.row;
                if (a.x != b.x)
                    return a.x < b.x;
                return a.row < b.row;
            });

            out.clearQuick();
            for (auto& item : keyed)
                out.add (item.c);
        }

        juce::String titleText;
        juce::Colour panelColour;
        bool escapeCloses = true;
        juce::uint32 lastTabMs = 0;
        juce::Point<int> contentSize;
        std::unique_ptr<juce::Component> content;
        juce::TextButton closeButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppModalOverlay)
    };

    juce::Component* launchAppDialog (juce::DialogWindow::LaunchOptions& options)
    {
        auto* host = options.componentToCentreAround;
        if (host == nullptr)
            return nullptr;

        auto* overlay = new AppModalOverlay (options);
        host->addAndMakeVisible (overlay);
        overlay->setBounds (host->getLocalBounds());
        overlay->toFront (true);
        overlay->enterModalState (true, nullptr, true);

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AppModalOverlay> (overlay)] {
            if (safe == nullptr)
                return;
            safe->toFront (true);
            safe->cycleFocus (true);
        });

        return overlay;
    }

    bool forwardTabToAppModal (juce::Component& host, const juce::KeyPress& key)
    {
        if (! key.isKeyCode (juce::KeyPress::tabKey))
            return false;

        for (int i = 0; i < host.getNumChildComponents(); ++i)
            if (auto* overlay = dynamic_cast<AppModalOverlay*> (host.getChildComponent (i)))
                return overlay->cycleFocus (! key.getModifiers().isShiftDown());

        return false;
    }

    bool nudgeFocusedSlider (const juce::KeyPress& key)
    {
        const bool up = key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::rightKey);
        const bool down = key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::leftKey);
        if (! up && ! down)
            return false;

        if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
            return false;

        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr)
            return false;

        auto* slider = dynamic_cast<juce::Slider*> (focused);
        if (slider == nullptr)
            slider = focused->findParentComponentOfClass<juce::Slider>();
        if (slider == nullptr)
            return false;

        double step = slider->getInterval();
        if (step <= 0.0)
            step = 0.1;
        if (key.getModifiers().isShiftDown())
            step *= 10.0;

        slider->setValue (slider->getValue() + (up ? step : -step), juce::sendNotificationSync);
        return true;
    }

    /** Ensure Enter-default (or first) Alert button actually has keyboard focus. */
    void focusDefaultAlertButton (juce::AlertWindow& aw)
    {
        juce::Button* fallback = nullptr;

        for (int i = 0; i < aw.getNumButtons(); ++i)
        {
            auto* b = aw.getButton (i);
            if (b == nullptr)
                continue;

            if (fallback == nullptr)
                fallback = b;

            if (b->isRegisteredForShortcut (juce::KeyPress (juce::KeyPress::returnKey)))
            {
                b->grabKeyboardFocus();
                return;
            }
        }

        if (fallback != nullptr)
            fallback->grabKeyboardFocus();
    }

    void showAlertAndFocusDefault (juce::AlertWindow* aw, juce::ModalComponentManager::Callback* callback)
    {
        aw->enterModalState (true, callback, true);
        // Peer / modal focus transfer may run after this call — re-assert on next tick.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::AlertWindow> (aw)] {
            if (safe != nullptr)
                focusDefaultAlertButton (*safe);
        });
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

MainComponent::MainComponent (juce::String projectPathToOpen, StartupProgress* progress)
    : menuBar (this),
      startupProgress (progress),
      startupProjectPath (std::move (projectPathToOpen)),
      mixer (engine, *this)
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setWantsKeyboardFocus (true);
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

    engineButton.onClick = [this] { setAudioEngineRunning (! audioEngineRunning); };
    addTrackButton.onClick = [this] {
        {
            const juce::ScopedLock sl (engine.getCallbackLock());
            engine.addTrack (jp (u8"トラック ") + juce::String (trackSerial++));
        }
        rebuildStrips();
        markProjectDirty();
        controlSurface.refreshFeedback();
    };

    addAndMakeVisible (engineButton);
    addAndMakeVisible (addTrackButton);

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
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
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
    engineButton.setBounds (header.removeFromRight (100).reduced (2, 6));
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

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (forwardTabToAppModal (*this, key))
        return true;

    if (nudgeFocusedSlider (key))
        return true;

    if (! key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
        return false;

    switch (key.getKeyCode())
    {
        case 'n':
        case 'N':
            menuItemSelected (menuNew, 0);
            return true;
        case 'o':
        case 'O':
            menuItemSelected (menuOpen, 0);
            return true;
        case 's':
        case 'S':
            if (key.getModifiers().isShiftDown())
                menuItemSelected (menuSaveAs, 0);
            else
                menuItemSelected (menuSave, 0);
            return true;
        default:
            break;
    }

    return false;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { jp (u8"ファイル"), jp (u8"オプション"), jp (u8"ヘルプ") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 1)
    {
        menu.addItem (menuAudioSettings, jp (u8"オーディオ設定..."));
        menu.addItem (menuSurfaceSettings, jp (u8"サーフェス..."));
        menu.addItem (menuMidiLearnSettings, jp (u8"MIDI学習..."));
        menu.addItem (menuVstScan, jp (u8"VST3 スキャン..."), ! scanInProgress);
        menu.addSeparator();
        menu.addItem (menuOptionsGeneral, jp (u8"一般..."));
        return menu;
    }

    if (topLevelMenuIndex == 2)
    {
        menu.addItem (menuSetupWizard, jp (u8"セットアップウィザード..."));
        return menu;
    }

    auto addFileItem = [&menu] (int id, juce::String text, bool enabled, juce::String shortcut)
    {
        juce::PopupMenu::Item item;
        item.itemID = id;
        item.text = std::move (text);
        item.isEnabled = enabled;
        item.shortcutKeyDescription = std::move (shortcut);
        menu.addItem (std::move (item));
    };

    addFileItem (menuNew, jp (u8"新規プロジェクト"), true, "Ctrl+N");
    addFileItem (menuOpen, jp (u8"開く..."), true, "Ctrl+O");
    menu.addSeparator();
    addFileItem (menuSave, jp (u8"保存"), appSettings.currentProject != juce::File(), "Ctrl+S");
    addFileItem (menuSaveAs, jp (u8"名前を付けて保存..."), true, "Ctrl+Shift+S");
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
    addFileItem (menuQuit, jp (u8"終了"), true, "Alt+F4");
    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == menuNew)
    {
        promptIfProjectDirty ([this] { newProject(); });
    }
    else if (menuItemID == menuOpen)
    {
        promptIfProjectDirty ([this] { openProject(); });
    }
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
    else if (menuItemID == menuAudioSettings)
        showAudioSettings();
    else if (menuItemID == menuSurfaceSettings)
        showSurfaceSettings();
    else if (menuItemID == menuMidiLearnSettings)
        showMidiLearnSettings();
    else if (menuItemID == menuVstScan)
        showScanDialog();
    else if (menuItemID == menuOptionsGeneral)
        showOptionsGeneral();
    else if (menuItemID == menuQuit)
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }
    else if (menuItemID >= menuRecentBase && menuItemID < menuRecentClear)
    {
        const int index = menuItemID - menuRecentBase;
        promptIfProjectDirty ([this, index] { openRecentProject (index); });
    }
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

void MainComponent::scanFinished (int failedCount)
{
    pruneMissingPlugins();
    savePluginList();
    const auto count = knownPlugins.getNumTypes();
    scanStatus.clear();

    auto text = jp (u8"VST3 ") + juce::String (count) + jp (u8" 個を登録しました");
    if (failedCount > 0)
        text += jp (u8"（読み込み失敗 ") + juce::String (failedCount) + jp (u8"）");
    status.setText (text, juce::dontSendNotification);
    scanInProgress = false;
    menuItemsChanged();

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
    options.useNativeTitleBar = false;
    options.componentToCentreAround = this;
    options.resizable = true;
    launchAppDialog (options);
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

    // 有効な入力のうち序数がいちばん若いチャンネルをモノラルで選ぶ（IN1/IN2 なら IN1）
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
    options.useNativeTitleBar = false;
    options.componentToCentreAround = this;
    options.resizable = true;

    auto* window = launchAppDialog (options);
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
    options.useNativeTitleBar = false;
    options.componentToCentreAround = this;
    options.resizable = false;

    auto* window = launchAppDialog (options);
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
    options.useNativeTitleBar = false;
    options.componentToCentreAround = this;
    options.resizable = false;

    auto* window = launchAppDialog (options);
    panelPtr->onClose = [window] {
        if (window != nullptr)
            window->exitModalState (0);
    };
    panelPtr->onOk = [this] {
        saveAppSettings();
        status.setText (makeStatusText(), juce::dontSendNotification);
    };
}

void MainComponent::showOptionsGeneral()
{
    auto panel = std::make_unique<OptionsGeneralPanel> (mixer.getExclusiveSoloMode());
    auto* panelPtr = panel.get();
    panel->setSize (480, 200);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = jp (u8"一般");
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.componentToCentreAround = this;
    options.resizable = false;

    auto* window = launchAppDialog (options);
    panelPtr->onClose = [window] {
        if (window != nullptr)
            window->exitModalState (0);
    };
    panelPtr->onOk = [this, panelPtr] {
        mixer.setExclusiveSoloMode (panelPtr->getExclusiveSolo());
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
                                        jp (u8"先に「オプション → MIDI学習」で入力デバイスを有効にしてください。"));
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
    options.useNativeTitleBar = false;
    options.componentToCentreAround = this;
    options.resizable = true;

    auto* window = launchAppDialog (options);
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

    pruneMissingPlugins();

    scanInProgress = true;
    menuItemsChanged();
    setScanStatus (jp (u8"VST3 をスキャンしています..."));
    scanThread = std::make_unique<PluginScanThread> (*this, knownPlugins, *format, paths);
    scanThread->startThread();
}

void MainComponent::pruneMissingPlugins()
{
    const auto types = knownPlugins.getTypes();
    for (int i = types.size(); --i >= 0;)
    {
        const auto& type = types.getReference (i);
        if (! formatManager.doesPluginStillExist (type))
            knownPlugins.removeType (type);
    }
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
    juce::KnownPluginList::addToMenu (menu, types, juce::KnownPluginList::sortByManufacturer);

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this, types, trackId, master] (int result) {
                            const int index = juce::KnownPluginList::getIndexChosenByMenu (types, result);
                            if (index < 0)
                                return;
                            attachPlugin (types.getReference (index), trackId, master);
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
        safe->markProjectDirty();
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
    markProjectDirty();
}

void MainComponent::removePluginFromMaster (int index)
{
    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        closeEditorsFor (engine.masterPlugins().get (index));
        engine.masterPlugins().remove (index);
    }
    rebuildStrips();
    markProjectDirty();
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
    markProjectDirty();
}

void MainComponent::beginTrackDrag (TrackStrip& strip)
{
    startDragging (juce::String (TrackStrip::dragType) + ":" + strip.getTrackId().toString(), &strip);
}

void MainComponent::beginPluginDrag (const juce::Uuid& trackId, int pluginIndex, juce::Component& source)
{
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (&source))
        container->startDragging (juce::String (TrackStrip::pluginDragType) + ":" + trackId.toString()
                                      + ":" + juce::String (pluginIndex),
                                  &source);
    else
        startDragging (juce::String (TrackStrip::pluginDragType) + ":" + trackId.toString()
                           + ":" + juce::String (pluginIndex),
                       &source);
}

void MainComponent::beginMasterPluginDrag (int pluginIndex, juce::Component& source)
{
    const auto desc = juce::String (TrackStrip::pluginDragType) + ":master:" + juce::String (pluginIndex);
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (&source))
        container->startDragging (desc, &source);
    else
        startDragging (desc, &source);
}

void MainComponent::transferPlugin (const juce::Uuid& fromTrackId, int pluginIndex,
                                    const juce::Uuid& toTrackId, int insertIndex)
{
    if (! juce::isPositiveAndBelow (pluginIndex, PluginChain::maxPlugins))
        return;

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        auto* from = engine.findTrack (fromTrackId);
        auto* to = engine.findTrack (toTrackId);
        if (from == nullptr || to == nullptr)
            return;

        if (fromTrackId == toTrackId)
        {
            if (! from->plugins.move (pluginIndex, insertIndex))
                return;
        }
        else
        {
            if (! to->plugins.canAdd())
            {
                juce::MessageManager::callAsync ([] {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::InfoIcon, "LiteHost",
                        jp (u8"VST はトラック／メインアウトあたり最大 10 個までです。"));
                });
                return;
            }

            bool bypassed = false;
            auto plugin = from->plugins.take (pluginIndex, bypassed);
            if (plugin == nullptr)
                return;

            to->plugins.insertPrepared (insertIndex, std::move (plugin), bypassed);
        }
    }

    rebuildStrips();
    markProjectDirty();
}

void MainComponent::reorderMasterPlugin (int pluginIndex, int insertIndex)
{
    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        if (! engine.masterPlugins().move (pluginIndex, insertIndex))
            return;
    }

    if (masterStrip != nullptr)
        masterStrip->refreshPlugins();
    markProjectDirty();
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
    markProjectDirty();
}

bool MainComponent::applySavedWindowState (juce::ResizableWindow& window)
{
    if (appSettings.windowState.isEmpty())
        return false;

    return window.restoreWindowStateFromString (appSettings.windowState);
}

bool MainComponent::requestQuit()
{
    if (quitConfirmed)
        return true;

    if (quitConfirmOpen)
        return false;

    auto beginQuit = [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe == nullptr)
            return;
        safe->quitConfirmed = true;
        if (auto* top = safe->getTopLevelComponent())
            top->setVisible (false);
        juce::MessageManager::callAsync ([] {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        });
    };

    // Non-dirty: hide first, then quit async — avoids disappear/flash/quit flicker.
    if (! projectDirty)
    {
        beginQuit();
        return false;
    }

    quitConfirmOpen = true;

    auto* aw = new juce::AlertWindow ("LiteHost",
                                      jp (u8"変更を保存しますか？"),
                                      juce::MessageBoxIconType::QuestionIcon,
                                      this);
    // はい = 保存して終了 / いいえ = 破棄して終了 / キャンセル = 終了しない
    aw->addButton (jp (u8"はい"), 1,
                   juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton (jp (u8"いいえ"), 2);
    aw->addButton (jp (u8"キャンセル"), 0,
                   juce::KeyPress (juce::KeyPress::escapeKey));

    showAlertAndFocusDefault (aw, juce::ModalCallbackFunction::create (
                                   [safe = juce::Component::SafePointer<MainComponent> (this), beginQuit] (int result) {
                                       if (safe == nullptr)
                                           return;
                                       safe->quitConfirmOpen = false;
                                       if (result == 1)
                                       {
                                           if (safe->appSettings.currentProject != juce::File()
                                               && safe->appSettings.currentProject.hasWriteAccess())
                                           {
                                               if (safe->saveProjectFile (safe->appSettings.currentProject))
                                                   beginQuit();
                                           }
                                           else
                                           {
                                               safe->saveProjectAsThen (beginQuit);
                                           }
                                       }
                                       else if (result == 2)
                                       {
                                           beginQuit();
                                       }
                                   }));

    return false;
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
                             : juce::String ("0.1.3");

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

    pruneMissingPlugins();
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
    if (projectDirty)
        titleText = "* " + titleText;

    if (auto* top = getTopLevelComponent())
        if (auto* window = dynamic_cast<juce::DocumentWindow*> (top))
            window->setName (titleText);
}

void MainComponent::projectEdited()
{
    markProjectDirty();
}

void MainComponent::markProjectDirty()
{
    if (suppressProjectDirty)
        return;

    if (! projectDirty)
    {
        projectDirty = true;
        updateWindowTitle();
    }
}

void MainComponent::clearProjectDirty()
{
    if (! projectDirty)
        return;

    projectDirty = false;
    updateWindowTitle();
}

void MainComponent::promptIfProjectDirty (std::function<void()> proceed)
{
    if (! projectDirty)
    {
        proceed();
        return;
    }

    auto* aw = new juce::AlertWindow ("LiteHost",
                                      jp (u8"プロジェクトに保存されていない変更があります。"),
                                      juce::MessageBoxIconType::QuestionIcon,
                                      this);
    aw->addButton (jp (u8"保存"), 1,
                   juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton (jp (u8"破棄"), 2);
    aw->addButton (jp (u8"キャンセル"), 0,
                   juce::KeyPress (juce::KeyPress::escapeKey));

    showAlertAndFocusDefault (aw, juce::ModalCallbackFunction::create (
                                   [safe = juce::Component::SafePointer<MainComponent> (this), proceed] (int result) {
                                       if (safe == nullptr)
                                           return;

                                       if (result == 2)
                                       {
                                           proceed();
                                           return;
                                       }

                                       if (result != 1)
                                           return;

                                       if (safe->appSettings.currentProject != juce::File()
                                           && safe->appSettings.currentProject.hasWriteAccess())
                                       {
                                           if (safe->saveProjectFile (safe->appSettings.currentProject))
                                               proceed();
                                       }
                                       else
                                       {
                                           safe->saveProjectAsThen (proceed);
                                       }
                                   }));
}

void MainComponent::clearProjectState()
{
    editorWindows.clear();
    strips.clear();
    trackList.removeAllChildren();

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        engine.clearTracksAndMaster();
    }

    if (masterStrip != nullptr)
        masterStrip->refreshPlugins();

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
    // Only use the startup splash during app launch. Creating another top-level
    // "LiteHost" window mid-session races the main window and can crash on reload.
    StartupProgress* progress = startupProgress;
    if (progress == nullptr && pluginTotal > 0)
        status.setText (jp (u8"プロジェクトを開いています: ") + file.getFileName(),
                        juce::dontSendNotification);

    if (progress != nullptr)
        progress->setStatus (jp (u8"プロジェクトを開いています: ") + file.getFileName(), 0.30);

    suppressProjectDirty = true;

    // Stop audio before tearing down plugins / tracks (callback may still be running).
    CrashLog::write ("loadProject closeAudioDevice " + file.getFileName());
    deviceManager.closeAudioDevice();
    clearProjectState();

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
    else if (pluginTotal > 0)
        status.setText (jp (u8"オーディオを再開しています..."), juce::dontSendNotification);

    deviceManager.restartLastAudioDevice();

    for (auto* plugin : delayedArm)
        armPluginAfterLaunch (plugin);

    CrashLog::write ("loadProject done " + file.getFileName());
    suppressProjectDirty = false;
    clearProjectDirty();
    status.setText (makeStatusText(), juce::dontSendNotification);
    return true;
}

bool MainComponent::saveProjectFile (const juce::File& file)
{
    if (! ProjectStore::saveToFile (file, engine))
        return false;

    rememberProject (file);
    clearProjectDirty();
    return true;
}

void MainComponent::newProject()
{
    suppressProjectDirty = true;
    deviceManager.closeAudioDevice();
    clearProjectState();
    appSettings.currentProject = juce::File();
    ensureDefaultTrack();
    rebuildStrips();
    deviceManager.restartLastAudioDevice();
    saveAppSettings();
    suppressProjectDirty = false;
    clearProjectDirty();
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
    saveProjectAsThen ({});
}

void MainComponent::saveProjectAsThen (std::function<void()> afterSave)
{
    auto chooser = std::make_shared<juce::FileChooser> (jp (u8"名前を付けて保存"),
                                                        getDefaultProjectsDir().getChildFile ("Untitled.litehost"),
                                                        "*.litehost");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser, afterSave] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".litehost"))
                                  file = file.withFileExtension (".litehost");
                              if (! saveProjectFile (file))
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "LiteHost",
                                                                          jp (u8"プロジェクトの保存に失敗しました。"));
                                  return;
                              }
                              if (afterSave)
                                  afterSave();
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
    // Project is saved only via explicit Save — not on quit.
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
