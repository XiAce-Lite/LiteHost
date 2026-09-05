#pragma once

#include <JuceHeader.h>

inline juce::String jp (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}

inline juce::String jp (const char8_t* utf8)
{
    return juce::String::fromUTF8 (reinterpret_cast<const char*> (utf8));
}
