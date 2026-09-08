#include "MainComponent.h"
#include "AppModalDialog.h"
#include "CrashLog.h"
#include "MixerStrips.h"
#include "PluginEditorWindow.h"
#include "StartupSplash.h"
#include "Utf8.h"
#include "app/AppPaths.h"
#include "app/ProjectStore.h"

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

    AppModalDialog::showAlertAndFocusDefault (aw, juce::ModalCallbackFunction::create (
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
                                                                                      : AppPaths::defaultProjectsDirectory(),
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
                                                        AppPaths::defaultProjectsDirectory().getChildFile ("Untitled.litehost"),
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

