#include "PluginChain.h"

bool PluginChain::prepareInstance (juce::AudioPluginInstance& plugin,
                                   double newSampleRate,
                                   int samplesPerBlock,
                                   bool releaseFirst,
                                   juce::AudioPlayHead* playHeadToUse)
{
    if (newSampleRate <= 0.0 || samplesPerBlock <= 0)
        return false;

    if (releaseFirst)
        plugin.releaseResources();

    // Do NOT force-enable every output bus (breaks multi-out drums like SSD).
    // Only ensure the main output exists when the plugin currently has no outputs.
    if (plugin.getTotalNumOutputChannels() == 0)
    {
        auto layout = plugin.getBusesLayout();
        if (layout.outputBuses.isEmpty())
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
        else
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        plugin.setBusesLayout (layout);
    }

    // Do NOT call setPlayConfigDetails() here.
    // SyncRoom has 1 stereo in + 10 stereo out buses (2/20 total). Forcing a flat
    // channel count collapses that layout and processBlock then access-violates.
    if (playHeadToUse != nullptr)
        plugin.setPlayHead (playHeadToUse);

    plugin.setRateAndBufferSizeDetails (newSampleRate, samplesPerBlock);
    plugin.setProcessingPrecision (juce::AudioProcessor::singlePrecision);
    plugin.setNonRealtime (false);
    plugin.prepareToPlay (newSampleRate, samplesPerBlock);
    return true;
}

void PluginChain::setPlayHead (juce::AudioPlayHead* newPlayHead)
{
    playHead = newPlayHead;
    for (auto& slot : slots)
        if (slot.plugin != nullptr)
            slot.plugin->setPlayHead (playHead);
}

void PluginChain::prepare (double newSampleRate, int samplesPerBlock, juce::AudioPlayHead* playHeadToUse)
{
    sampleRate = newSampleRate;
    blockSize = samplesPerBlock;
    prepared = true;
    if (playHeadToUse != nullptr)
        playHead = playHeadToUse;

    // Size for the device block (plus headroom). Avoid permanently allocating 128×8192
    // just to clear it every callback — that alone can push DSP load into the danger zone.
    scratch.setSize (maxScratchChannels, juce::jmax (samplesPerBlock, 2048), false, true, true);

    for (auto& slot : slots)
    {
        if (slot.plugin == nullptr)
            continue;

        const bool wasSuspended = slot.plugin->isSuspended();
        const bool alreadyLive = slot.plugin->getSampleRate() > 0.0
                              && slot.plugin->getBlockSize() > 0;
        const bool settingsChanged = ! juce::approximatelyEqual (slot.plugin->getSampleRate(), sampleRate)
                                  || slot.plugin->getBlockSize() != blockSize;

        // SyncRoom stays prepared across device close/open. Calling
        // releaseResources() then skipping prepareToPlay (JUCE still reports the
        // old rate/block) left it silent until the instance was destroyed.
        // Re-prepare with releaseFirst also crashes it during app launch.
        if (shouldKeepPrepared (*slot.plugin) && ! slot.needsPrepare)
        {
            if (playHead != nullptr)
                slot.plugin->setPlayHead (playHead);

            if (settingsChanged)
                prepareInstance (*slot.plugin, sampleRate, blockSize, false, playHead);

            refreshSlotChannels (slot);
            slot.plugin->suspendProcessing (wasSuspended);
            continue;
        }

        // After a real release, getSampleRate()/getBlockSize() usually stay set,
        // so "already live" would skip prepareToPlay and leave the plug dead.
        if (slot.needsPrepare || ! alreadyLive || settingsChanged)
            prepareInstance (*slot.plugin, sampleRate, blockSize,
                             alreadyLive && settingsChanged && ! slot.needsPrepare,
                             playHead);

        slot.needsPrepare = false;

        if (playHead != nullptr)
            slot.plugin->setPlayHead (playHead);

        refreshSlotChannels (slot);
        slot.plugin->suspendProcessing (wasSuspended);
    }
}

void PluginChain::release()
{
    prepared = false;
    for (auto& slot : slots)
    {
        if (slot.plugin == nullptr)
            continue;

        if (shouldKeepPrepared (*slot.plugin))
            continue;

        slot.plugin->releaseResources();
        slot.needsPrepare = true;
    }
}

void PluginChain::refreshSlotChannels (Slot& slot) noexcept
{
    if (slot.plugin == nullptr)
        return;

    slot.numIns = slot.plugin->getTotalNumInputChannels();
    slot.numOuts = slot.plugin->getTotalNumOutputChannels();
}

bool PluginChain::shouldKeepPrepared (const juce::AudioPluginInstance& plugin) noexcept
{
    return plugin.getName().containsIgnoreCase ("syncroom");
}

void PluginChain::processSlot (Slot& slot, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& incomingMidi) noexcept
{
    auto* plugin = slot.plugin.get();
    if (plugin == nullptr || plugin->isSuspended() || slot.bypassed)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    if (numSamples > scratch.getNumSamples())
        scratch.setSize (maxScratchChannels, numSamples, false, false, true);

    const int hostChans = buffer.getNumChannels();
    const int pluginChans = juce::jmax (1, juce::jmax (slot.numIns, slot.numOuts));

    // Copy only when there are events (instruments consume; empty FX path stays cheap).
    if (incomingMidi.isEmpty())
    {
        if (! midi.isEmpty())
            midi.clear();
    }
    else
    {
        midi = incomingMidi;
    }

    // Fast path for ordinary stereo inserts / stereo instruments.
    if (pluginChans <= hostChans)
    {
        const juce::ScopedLock pluginLock (plugin->getCallbackLock());
        if (! plugin->isSuspended())
            plugin->processBlock (buffer, midi);
        return;
    }

    if (pluginChans > scratch.getNumChannels())
    {
        // Last resort: process only as many channels as we can (still better than silence).
        // Prefer growing if possible without exceeding a hard cap.
        return;
    }

    // JUCE VST3 maps in+out in-place from channel 0: buffer needs max(ins, outs) channels.
    // Never scratch.clear() the whole allocation (was 128ch × 8192 every block → major xruns).
    const int copyIn = juce::jmin (hostChans, juce::jmax (0, slot.numIns));
    for (int ch = 0; ch < copyIn; ++ch)
        scratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    if (hostChans == 1 && slot.numIns >= 2)
    {
        scratch.copyFrom (1, 0, scratch, 0, 0, numSamples);
        for (int ch = 2; ch < pluginChans; ++ch)
            juce::FloatVectorOperations::clear (scratch.getWritePointer (ch), numSamples);
    }
    else
    {
        for (int ch = copyIn; ch < pluginChans; ++ch)
            juce::FloatVectorOperations::clear (scratch.getWritePointer (ch), numSamples);
    }

    {
        const juce::ScopedLock pluginLock (plugin->getCallbackLock());
        if (plugin->isSuspended())
            return;

        juce::AudioBuffer<float> pluginBuffer (scratch.getArrayOfWritePointers(), pluginChans, numSamples);
        plugin->processBlock (pluginBuffer, midi);
    }

    // Main stereo from the first output bus only.
    // Do not sum multi-outs: drum kits often put the same hit on several buses and summing causes harsh overload/phase junk.
    buffer.clear();
    const int copyOut = juce::jmin (hostChans, 2, juce::jmax (0, slot.numOuts));
    for (int ch = 0; ch < copyOut; ++ch)
        buffer.copyFrom (ch, 0, scratch, ch, 0, numSamples);

    if (copyOut == 1 && hostChans >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void PluginChain::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& incomingMidi) noexcept
{
    for (auto& slot : slots)
        processSlot (slot, buffer, incomingMidi);
}

juce::AudioPluginInstance* PluginChain::addPrepared (std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    if (plugin == nullptr || ! canAdd())
        return nullptr;

    if (playHead != nullptr)
        plugin->setPlayHead (playHead);

    Slot slot;
    slot.description = plugin->getPluginDescription();
    slot.plugin = std::move (plugin);
    refreshSlotChannels (slot);

    auto* raw = slot.plugin.get();
    slots.push_back (std::move (slot));
    return raw;
}

void PluginChain::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    if (auto* plugin = slots[(size_t) index].plugin.get())
        plugin->releaseResources();

    slots.erase (slots.begin() + index);
}

void PluginChain::clear()
{
    for (auto& slot : slots)
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();

    slots.clear();
}

void PluginChain::setBypassed (int index, bool shouldBeBypassed)
{
    if (juce::isPositiveAndBelow (index, (int) slots.size()))
        slots[(size_t) index].bypassed = shouldBeBypassed;
}

bool PluginChain::isBypassed (int index) const noexcept
{
    if (juce::isPositiveAndBelow (index, (int) slots.size()))
        return slots[(size_t) index].bypassed;

    return false;
}

juce::AudioPluginInstance* PluginChain::get (int index) const noexcept
{
    if (juce::isPositiveAndBelow (index, (int) slots.size()))
        return slots[(size_t) index].plugin.get();

    return nullptr;
}

juce::PluginDescription PluginChain::descriptionAt (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) slots.size()))
        return slots[(size_t) index].description;

    return {};
}

void PluginChain::writeXml (juce::XmlElement& parent) const
{
    for (auto& slot : slots)
    {
        if (slot.plugin == nullptr)
            continue;

        auto xml = slot.description.createXml();
        if (xml == nullptr)
            continue;

        juce::MemoryBlock state;
        slot.plugin->getStateInformation (state);
        xml->setAttribute ("state", state.toBase64Encoding());
        xml->setAttribute ("bypass", slot.bypassed ? 1 : 0);
        parent.addChildElement (xml.release());
    }
}

std::unique_ptr<juce::AudioPluginInstance> PluginChain::instantiate (
    const juce::PluginDescription& description,
    juce::AudioPluginFormatManager& formats,
    double sampleRate,
    int blockSize,
    juce::String& error)
{
    // Do not alter buses here — SyncRoom (2in/20out) must keep its native layout.
    return formats.createPluginInstance (description, sampleRate, blockSize, error);
}
