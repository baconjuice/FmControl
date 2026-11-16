cmake_minimum_required(VERSION 3.22)

project(FmControl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Core library: SysEx codec, patch model, parameter tables (no GUI)
# ---------------------------------------------------------------------------
add_library(fm_core STATIC
    Source/fm/Patch.cpp
    Source/fm/InitPatch.cpp
    Source/fm/Params.cpp
    Source/fm/CcNames.cpp
    Source/fm/SysEx.cpp
    Source/fm/SyxFile.cpp)
target_include_directories(fm_core PUBLIC Source)
target_compile_features(fm_core PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# The app / plugin
# ---------------------------------------------------------------------------
juce_add_plugin(FmControl
    COMPANY_NAME "FmControl"
    PRODUCT_NAME "FmControl"
    BUNDLE_ID "com.fmcontrol.fmcontrol"
    PLUGIN_MANUFACTURER_CODE Fmct
    PLUGIN_CODE Fmv1
    FORMATS Standalone AU VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(FmControl PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/device/FmDevice.cpp
    Source/device/PacedSysEx.cpp
    Source/model/Session.cpp
    Source/model/MidiLearn.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/ParamControls.cpp
    Source/ui/EditorCommon.cpp
    Source/ui/ConnectionBar.cpp
    Source/ui/LibraryPanel.cpp
    Source/ui/VoiceEditor.cpp)

target_compile_definitions(FmControl PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FmControl
    PRIVATE
        fm_core
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_executable(fm_tests Tests/CoreTests.cpp)
target_link_libraries(fm_tests PRIVATE fm_core)
target_compile_definitions(fm_tests PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")
add_test(NAME fm_tests COMMAND fm_tests)

# ---------------------------------------------------------------------------
# fmcli: command-line diagnostics (port scan, ping, patch read)
# ---------------------------------------------------------------------------
juce_add_console_app(fmcli PRODUCT_NAME "fmcli")
target_sources(fmcli PRIVATE Tools/FmCli.cpp Source/device/FmDevice.cpp Source/device/PacedSysEx.cpp
               Source/model/Session.cpp Source/model/MidiLearn.cpp)
target_compile_definitions(fmcli PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(fmcli PRIVATE fm_core juce::juce_audio_devices juce::juce_events juce::juce_data_structures
                      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
