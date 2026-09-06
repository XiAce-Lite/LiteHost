#include "AppSettingsStore.h"
#include "AppPaths.h"
#include "audio/AudioEngine.h"
#include "control/ControlSurface.h"
#include "control/MidiLearn.h"

void AppSettingsStore::load (AudioEngine& engine,
                             ControlSurfaceManager& surface,
                             MidiLearnManager& learn)
{
    recentProjects.clear();
    extraVstPaths.clear();
    currentProject = juce::File();
    windowState.clear();
    skippedReleaseTag.clear();
    setupWizardCompleted = false;

    auto xml = juce::XmlDocument::parse (AppPaths::settingsFile());
    if (xml == nullptr || xml->getTagName() != "SETTINGS")
        return;

    // Existing installs without the flag are treated as already finished.
    setupWizardCompleted = xml->getBoolAttribute ("setupWizardCompleted", true);
    windowState = xml->getStringAttribute ("windowState");
    skippedReleaseTag = xml->getStringAttribute ("skippedReleaseTag");
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

    engine.exclusiveSoloMode = xml->getBoolAttribute ("exclusiveSoloMode", false);
    surface.readXml (*xml);
    learn.readXml (*xml);
}

void AppSettingsStore::save (const AudioEngine& engine,
                             const ControlSurfaceManager& surface,
                             const MidiLearnManager& learn) const
{
    juce::XmlElement xml ("SETTINGS");
    xml.setAttribute ("lastProject", currentProject.getFullPathName());
    xml.setAttribute ("setupWizardCompleted", setupWizardCompleted ? 1 : 0);
    xml.setAttribute ("windowState", windowState);
    xml.setAttribute ("skippedReleaseTag", skippedReleaseTag);
    xml.setAttribute ("exclusiveSoloMode", engine.exclusiveSoloMode.load() ? 1 : 0);

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

    surface.writeXml (xml);
    learn.writeXml (xml);
    xml.writeTo (AppPaths::settingsFile());
}

void AppSettingsStore::rememberProject (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    currentProject = file;
    recentProjects.removeAllInstancesOf (file);
    recentProjects.insert (0, file);

    while (recentProjects.size() > 10)
        recentProjects.removeLast();
}

void AppSettingsStore::clearRecent()
{
    recentProjects.clear();
}
