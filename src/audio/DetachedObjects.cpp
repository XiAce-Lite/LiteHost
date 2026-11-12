#include "DetachedObjects.h"
#include "TrackProcessor.h"

DetachedObjects::DetachedObjects() = default;
DetachedObjects::DetachedObjects (DetachedObjects&&) noexcept = default;

DetachedObjects& DetachedObjects::operator= (DetachedObjects&& other) noexcept
{
    if (this != &other)
    {
        destroyAll();
        tracks = std::move (other.tracks);
        plugins = std::move (other.plugins);
    }
    return *this;
}

DetachedObjects::~DetachedObjects()
{
    destroyAll();
}

void DetachedObjects::add (std::unique_ptr<TrackProcessor> track)
{
    if (track != nullptr)
        tracks.push_back (std::move (track));
}

void DetachedObjects::add (std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    if (plugin != nullptr)
        plugins.push_back (std::move (plugin));
}

void DetachedObjects::add (std::vector<std::unique_ptr<juce::AudioPluginInstance>> pluginsToAdd)
{
    for (auto& plugin : pluginsToAdd)
        add (std::move (plugin));
}

void DetachedObjects::destroyAll()
{
    for (auto& plugin : plugins)
        plugin->releaseResources();

    plugins.clear();
    tracks.clear();
}
