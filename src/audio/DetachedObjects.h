#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

class TrackProcessor;

/** Collects tracks / plugins detached from the graph under the audio callback lock.
    Destruction (releaseResources + delete, which can take a long time) happens when this
    object dies; AudioEngine::editGraph guarantees that is after the lock is released. */
class DetachedObjects
{
public:
    DetachedObjects();
    ~DetachedObjects();
    DetachedObjects (DetachedObjects&&) noexcept;
    DetachedObjects& operator= (DetachedObjects&&) noexcept;

    void add (std::unique_ptr<TrackProcessor> track);
    void add (std::unique_ptr<juce::AudioPluginInstance> plugin);
    void add (std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins);

    /** Destroy everything now (plugins are released first). Never call under the callback lock. */
    void destroyAll();

private:
    std::vector<std::unique_ptr<TrackProcessor>> tracks;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins;
};
