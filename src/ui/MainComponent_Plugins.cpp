#include "MainComponent.h"
#include "MixerStrips.h"
#include "Utf8.h"
#include "control/ControlSurface.h"

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
                                    const juce::Uuid& toTrackId, int insertIndex, bool copy)
{
    if (! juce::isPositiveAndBelow (pluginIndex, PluginChain::maxPlugins))
        return;

    if (copy)
    {
        copyPlugin (fromTrackId, pluginIndex, toTrackId, insertIndex);
        return;
    }

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

void MainComponent::copyPlugin (const juce::Uuid& fromTrackId, int pluginIndex,
                                const juce::Uuid& toTrackId, int insertIndex)
{
    PluginChain::PluginLoadRequest request;

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        auto* from = engine.findTrack (fromTrackId);
        auto* to = engine.findTrack (toTrackId);
        if (from == nullptr || to == nullptr)
            return;

        if (! juce::isPositiveAndBelow (pluginIndex, from->plugins.size()))
            return;

        if (! to->plugins.canAdd())
        {
            juce::MessageManager::callAsync ([] {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::InfoIcon, "LiteHost",
                    jp (u8"VST はトラック／メインアウトあたり最大 10 個までです。"));
            });
            return;
        }

        auto* plugin = from->plugins.get (pluginIndex);
        if (plugin == nullptr)
            return;

        request.description = from->plugins.descriptionAt (pluginIndex);
        plugin->getStateInformation (request.state);
        request.hasState = request.state.getSize() > 0;
        request.bypass = from->plugins.isBypassed (pluginIndex);
    }

    // Instantiation must not sit under the audio lock; loadPluginIntoChain commits under it.
    auto* to = engine.findTrack (toTrackId);
    if (to == nullptr || ! to->plugins.canAdd())
        return;

    const auto result = loadPluginIntoChain (to->plugins, request, true);
    if (result.plugin == nullptr)
    {
        const auto message = result.error.isNotEmpty() && result.error != "prepare failed"
            ? result.error
            : jp (u8"プラグインの複製に失敗しました。");
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                jp (u8"プラグインを開けません"), message);
        return;
    }

    {
        const juce::ScopedLock sl (engine.getCallbackLock());
        auto* dest = engine.findTrack (toTrackId);
        if (dest == nullptr)
            return;

        const int addedAt = dest->plugins.size() - 1;
        if (addedAt >= 0 && insertIndex != addedAt && insertIndex != dest->plugins.size())
            dest->plugins.move (addedAt, insertIndex);
    }

    rebuildStrips();
    markProjectDirty();

    if (result.needsDelayedArm)
        armPluginAfterLaunch (result.plugin);
    else
        result.plugin->suspendProcessing (false);
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
        // Fixed-width CPU so trailing status bits don't jitter as the value crosses 9.99 / 99.99.
        // Integer part is always 3 columns (figure-space padded); fraction is always 2 digits.
        const double cpuPct = juce::jlimit (0.0, 999.99, (double) engine.getCpuPeakLoad() * 100.0);
        auto cpuText = juce::String::formatted ("%.2f", cpuPct);
        {
            constexpr juce::juce_wchar figureSpace = 0x2007;
            const int intDigits = juce::jmax (0, cpuText.indexOfChar ('.'));
            for (int i = intDigits; i < 3; ++i)
                cpuText = juce::String::charToString (figureSpace) + cpuText;
        }

        return device->getName() + "  |  "
             + juce::String (sr / 1000.0, 1) + " kHz  |  "
             + juce::String (bs) + " samples  |  "
             + juce::String (latencyMs, 1) + " ms  |  CPU "
             + cpuText + "%"
             + surfaceBit;
    }

    if (! audioEngineRunning)
        return jp (u8"オーディオ停止中") + surfaceBit;

    return jp (u8"オーディオデバイスが開かれていません") + surfaceBit;
}
