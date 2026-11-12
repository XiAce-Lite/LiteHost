#pragma once

#include <JuceHeader.h>

namespace JumpList
{
    /** Mirror the recent-project list into the taskbar Jump List (Windows only; no-op elsewhere).
        Message thread. Items launch LiteHost with the project path as its argument. */
    void setRecentProjects (const juce::Array<juce::File>& projects);
}
