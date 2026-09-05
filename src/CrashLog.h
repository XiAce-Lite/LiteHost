#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace CrashLog
{
    inline juce::File logFile()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("LiteHost");
        dir.createDirectory();
        return dir.getChildFile ("plugin_load.log");
    }

    inline void write (const juce::String& line)
    {
        const auto stamp = juce::Time::getCurrentTime().toString (true, true, true, true);
        const auto text = stamp + "  " + line + "\n";
        logFile().appendText (text, false, false);
        juce::Logger::writeToLog (line);
    }

    inline void clear()
    {
        logFile().replaceWithText ({});
    }

   #if JUCE_WINDOWS
    inline LONG WINAPI unhandledFilter (EXCEPTION_POINTERS* info)
    {
        write ("CRASH exception=0x"
               + juce::String::toHexString ((juce::uint32) info->ExceptionRecord->ExceptionCode)
               + " at "
               + juce::String::toHexString ((juce::pointer_sized_int) info->ExceptionRecord->ExceptionAddress));
        return EXCEPTION_EXECUTE_HANDLER;
    }

    inline void install()
    {
        SetUnhandledExceptionFilter (unhandledFilter);
    }
   #else
    inline void install() {}
   #endif
}
