#pragma once

#include <JuceHeader.h>
#include <vector>

class PluginChain
{
public:
    static constexpr int maxPlugins = 10;
    /** SSD などマルチアウト音源は 32ch を超えることがある */
    static constexpr int maxScratchChannels = 128;

    void prepare (double sampleRate, int samplesPerBlock, juce::AudioPlayHead* playHead = nullptr);
    void release();
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& incomingMidi) noexcept;
    void setPlayHead (juce::AudioPlayHead* newPlayHead);

    bool canAdd() const noexcept { return size() < maxPlugins; }

    /** Configure + prepare on the message thread before addPrepared(). */
    static bool prepareInstance (juce::AudioPluginInstance& plugin,
                                 double sampleRate,
                                 int blockSize,
                                 bool releaseFirst,
                                 juce::AudioPlayHead* playHead = nullptr);

    /** Insert an already-prepared instance. Does not call prepareToPlay. */
    juce::AudioPluginInstance* addPrepared (std::unique_ptr<juce::AudioPluginInstance> plugin);

    void remove (int index);
    void clear();
    void setBypassed (int index, bool shouldBeBypassed);
    bool isBypassed (int index) const noexcept;

    int size() const noexcept { return (int) slots.size(); }
    juce::AudioPluginInstance* get (int index) const noexcept;
    juce::PluginDescription descriptionAt (int index) const;

    void writeXml (juce::XmlElement& parent) const;
    static std::unique_ptr<juce::AudioPluginInstance> instantiate (
        const juce::PluginDescription& description,
        juce::AudioPluginFormatManager& formats,
        double sampleRate,
        int blockSize,
        juce::String& error);

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription description;
        int numIns = 2;
        int numOuts = 2;
        bool bypassed = false;
        /** True after releaseResources(); prepare() must call prepareToPlay again. */
        bool needsPrepare = false;
    };

    void processSlot (Slot& slot, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& incomingMidi) noexcept;
    void refreshSlotChannels (Slot& slot) noexcept;
    static bool shouldKeepPrepared (const juce::AudioPluginInstance& plugin) noexcept;

    std::vector<Slot> slots;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    juce::AudioPlayHead* playHead = nullptr;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool prepared = false;
};
