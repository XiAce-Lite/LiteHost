cmake_minimum_required(VERSION 3.22)
project(LiteHost VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(LITEHOST_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party")
set(LITEHOST_JUCE_DIR "${LITEHOST_THIRD_PARTY}/JUCE")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${LITEHOST_JUCE_DIR}
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(LiteHost
    PRODUCT_NAME "LiteHost"
    VERSION "0.1.1"
    COMPANY_NAME "LiteHost"
    BUNDLE_ID com.litehost.app
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Used to capture audio interface input"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

juce_add_binary_data(LiteHostAssets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        assets/icon.png
)

juce_generate_juce_header(LiteHost)

target_sources(LiteHost PRIVATE
    src/Main.cpp
    src/Utf8.h
    src/CrashLog.h
    src/app/AppPaths.h
    src/app/AppPaths.cpp
    src/app/AppSettingsStore.h
    src/app/AppSettingsStore.cpp
    src/app/ProjectStore.h
    src/app/ProjectStore.cpp
    src/app/PluginScanCoordinator.h
    src/app/PluginScanCoordinator.cpp
    src/session/MixerSession.h
    src/session/MixerSession.cpp
    src/audio/PluginChain.h
    src/audio/PluginChain.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/SyncRoomFinder.h
    src/audio/SyncRoomFinder.cpp
    src/control/ControlSurface.h
    src/control/ControlSurface.cpp
    src/control/MidiLearn.h
    src/control/MidiLearn.cpp
    src/ui/LookAndFeel.h
    src/ui/MixerWidgets.h
    src/ui/MixerWidgets.cpp
    src/ui/MixerStrips.h
    src/ui/MixerStrips.cpp
    src/ui/VstFolderPathList.h
    src/ui/VstFolderPathList.cpp
    src/ui/ScanFolderPanel.h
    src/ui/ScanFolderPanel.cpp
    src/ui/SetupWizardPanel.h
    src/ui/SetupWizardPanel.cpp
    src/ui/SettingsDialogs.h
    src/ui/SettingsDialogs.cpp
    src/ui/PluginEditorWindow.h
    src/ui/PluginEditorWindow.cpp
    src/ui/PluginScanThread.h
    src/ui/PluginScanThread.cpp
    src/ui/UpdateChecker.h
    src/ui/UpdateChecker.cpp
    src/ui/StartupSplash.h
    src/ui/StartupSplash.cpp
    src/ui/MainComponent.h
    src/ui/MainComponent.cpp
)

target_include_directories(LiteHost PRIVATE src)

target_compile_definitions(LiteHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

if(WIN32)
    target_compile_definitions(LiteHost PRIVATE
        JUCE_WASAPI=1
        JUCE_WASAPI_EXCLUSIVE=1
        JUCE_DIRECTSOUND=0
        JUCE_ASIO=1
        JUCE_ASIO_USE_EXTERNAL_SDK=0
    )
endif()

if(APPLE)
    # Same product as Windows: VST3 only. AU can be enabled later without a fork.
    target_compile_definitions(LiteHost PRIVATE
        JUCE_PLUGINHOST_AU=0
        JUCE_COREAUDIO=1
    )
endif()

if(MSVC)
    target_compile_options(LiteHost PRIVATE /utf-8 /fp:fast /W3)
endif()

target_link_libraries(LiteHost PRIVATE
    LiteHostAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
)
