cmake_minimum_required(VERSION 3.22)

project(OVERHEAT VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: static MSVC runtime so the plugin runs without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Windows: the UI runs in Edge WebView2 (statically linked loader, NuGet package)
set(OVERHEAT_PLATFORM_ARGS)
if(WIN32)
    list(APPEND OVERHEAT_PLATFORM_ARGS NEEDS_WEBVIEW2 TRUE)
endif()

juce_add_plugin(OVERHEAT
    ${OVERHEAT_PLATFORM_ARGS}
    COMPANY_NAME                "BUYORBEALOSER"
    COMPANY_WEBSITE             "https://github.com/mostwanted3488/OVERHEAT"
    BUNDLE_ID                   "com.buyorbealoser.overheat"
    PLUGIN_MANUFACTURER_CODE    Bobl
    PLUGIN_CODE                 Ovht
    PRODUCT_NAME                "OVERHEAT"
    DESCRIPTION                 "One-knob thermal saturation"
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER           TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect)

# The whole interface: one self-contained HTML file (built by tools/build_ui.py)
juce_add_binary_data(OVERHEAT_UI SOURCES ui/index.html)

target_sources(OVERHEAT PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(OVERHEAT PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(WIN32)
    target_compile_definitions(OVERHEAT PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

target_link_libraries(OVERHEAT
    PRIVATE
        OVERHEAT_UI
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
