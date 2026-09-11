cmake_minimum_required(VERSION 3.22)
project(LiteHost VERSION 0.1.9 LANGUAGES C CXX)

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
    VERSION "0.1.9"
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
    src/app/ScanUiSuppressor.h
    src/app/ScanUiSuppressor.cpp
    src/app/OutOfProcessPluginScanner.h
    src/app/OutOfProcessPluginScanner.cpp
    src/scanner/PluginScanIpc.h
    src/session/MixerSession.h
    src/session/MixerSession.cpp
    src/audio/PluginChain.h
    src/audio/PluginChain.cpp
    src/audio/HostPlayHead.h
    src/audio/TrackProcessor.h
    src/audio/TrackProcessor.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/SyncRoomFinder.h
    src/audio/SyncRoomFinder.cpp
    src/control/ControlSurface.h
    src/control/ControlSurface.cpp
    src/control/MidiLearn.h
    src/control/MidiLearn.cpp
    src/ui/LookAndFeel.h
    src/ui/LookAndFeel.cpp
    src/ui/AppModalDialog.h
    src/ui/AppModalDialog.cpp
    src/ui/MixerStripHost.h
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
    src/ui/MainComponent_Project.cpp
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
    # Same product as Windows: VST3 host only. AU can be enabled later without a fork.
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

# ---------------------------------------------------------------------------
# LiteHostScanner — out-of-process VST3 probe (crash-isolated)
# ---------------------------------------------------------------------------
juce_add_console_app(LiteHostScanner
    PRODUCT_NAME "LiteHostScanner"
    VERSION "0.1.9"
    COMPANY_NAME "LiteHost"
    BUNDLE_ID com.litehost.scanner
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

juce_generate_juce_header(LiteHostScanner)

target_sources(LiteHostScanner PRIVATE
    src/scanner/LiteHostScannerMain.cpp
    src/scanner/PluginScanIpc.h
    src/app/ScanUiSuppressor.h
    src/app/ScanUiSuppressor.cpp
)

target_include_directories(LiteHostScanner PRIVATE src)

target_compile_definitions(LiteHostScanner PRIVATE
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
    target_compile_definitions(LiteHostScanner PRIVATE
        JUCE_WASAPI=0
        JUCE_DIRECTSOUND=0
        JUCE_ASIO=0
    )
endif()

if(APPLE)
    target_compile_definitions(LiteHostScanner PRIVATE
        JUCE_PLUGINHOST_AU=0
        JUCE_COREAUDIO=0
    )
endif()

if(MSVC)
    target_compile_options(LiteHostScanner PRIVATE /utf-8 /W3)
endif()

target_link_libraries(LiteHostScanner PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

add_dependencies(LiteHost LiteHostScanner)

# Ship scanner next to the host (Windows dir / macOS Contents/MacOS).
add_custom_command(TARGET LiteHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:LiteHostScanner>
        $<TARGET_FILE_DIR:LiteHost>
    COMMENT "Copy LiteHostScanner next to LiteHost"
)
