#include "ProjectStore.h"
#include "Utf8.h"
#include "audio/AudioEngine.h"
#include "ui/StartupSplash.h"

int ProjectStore::countPlugins (const juce::XmlElement& root)
{
    int n = 0;
    if (auto* master = root.getChildByName ("MASTER"))
        n += PluginChain::countXmlPlugins (*master);

    if (auto* tracks = root.getChildByName ("TRACKS"))
        for (auto* track = tracks->getFirstChildElement(); track != nullptr; track = track->getNextElement())
            if (track->getTagName() == "TRACK")
                n += PluginChain::countXmlPlugins (*track);

    return n;
}

bool ProjectStore::loadIntoEngine (const juce::XmlElement& root,
                                   AudioEngine& engine,
                                   int& trackSerial,
                                   std::vector<juce::AudioPluginInstance*>& delayedArm,
                                   const LoadPluginFn& loadPlugin,
                                   StartupProgress* progress)
{
    const int pluginTotal = countPlugins (root);
    int pluginsLoaded = 0;

    auto restoreChain = [&] (PluginChain& chain, const juce::XmlElement& parent) {
        for (const auto& request : PluginChain::parseXml (parent))
        {
            if (! chain.canAdd())
                break;

            ++pluginsLoaded;
            if (progress != nullptr)
            {
                const double p = pluginTotal > 0
                    ? 0.32 + 0.58 * ((double) pluginsLoaded / (double) pluginTotal)
                    : 0.7;
                progress->setStatus (jp (u8"読み込み中: ") + request.description.name
                                         + "  (" + juce::String (pluginsLoaded)
                                         + "/" + juce::String (pluginTotal) + ")",
                                     p);
            }

            const auto result = loadPlugin (chain, request, false);
            if (result.plugin != nullptr && result.needsDelayedArm)
                delayedArm.push_back (result.plugin);
        }
    };

    if (auto* master = root.getChildByName ("MASTER"))
    {
        engine.reverbEnabled = master->getBoolAttribute ("reverb", false);
        engine.limiterEnabled = master->getBoolAttribute ("limiter", false);
        engine.gateEnabled = master->getBoolAttribute ("gate", false);
        engine.reverbWet = (float) master->getDoubleAttribute ("wet", 0.18);
        engine.reverbRoom = (float) master->getDoubleAttribute ("room", 0.42);
        engine.reverbDamping = (float) master->getDoubleAttribute ("damping", 0.4);
        engine.limiterThresholdDb = juce::jlimit (
            AudioEngine::limiterCeilingMinDb,
            AudioEngine::limiterCeilingMaxDb,
            (float) master->getDoubleAttribute ("ceiling", -0.3));
        engine.gateThresholdDb = (float) master->getDoubleAttribute ("gateThreshold", -52.0);
        engine.masterGain = (float) master->getDoubleAttribute ("gain", 1.0);
        restoreChain (engine.masterPlugins(), *master);
    }

    if (auto* tracks = root.getChildByName ("TRACKS"))
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
            track->soloOverride = child->getBoolAttribute ("soloOverride", false)
                               || child->getBoolAttribute ("exclusiveSolo", false);
            track->plugins.setChainBypassed (child->getBoolAttribute ("pluginsBypassed", false));
            restoreChain (track->plugins, *child);
            ++trackSerial;
        }
    }

    return true;
}

bool ProjectStore::saveToFile (const juce::File& file, const AudioEngine& engine)
{
    juce::XmlElement xml ("LITEHOST");
    xml.setAttribute ("version", 1);

    auto* master = xml.createNewChildElement ("MASTER");
    master->setAttribute ("reverb", engine.reverbEnabled.load());
    master->setAttribute ("limiter", engine.limiterEnabled.load());
    master->setAttribute ("gate", engine.gateEnabled.load());
    master->setAttribute ("wet", engine.reverbWet.load());
    master->setAttribute ("room", engine.reverbRoom.load());
    master->setAttribute ("damping", engine.reverbDamping.load());
    master->setAttribute ("ceiling", engine.limiterThresholdDb.load());
    master->setAttribute ("gateThreshold", engine.gateThresholdDb.load());
    master->setAttribute ("gain", engine.masterGain.load());
    engine.masterPlugins().writeXml (*master);

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
        child->setAttribute ("soloOverride", track->soloOverride.load());
        child->setAttribute ("pluginsBypassed", track->plugins.isChainBypassed() ? 1 : 0);
        track->plugins.writeXml (*child);
    }

    return xml.writeTo (file);
}
