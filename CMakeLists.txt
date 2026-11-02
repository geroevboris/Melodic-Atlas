cmake_minimum_required(VERSION 3.22)

# Автоматическая сборка Universal Binary (Apple Silicon arm64 + Intel x86_64) для macOS
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal architectures" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version" FORCE)
endif()

project(MelodicAtlas VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. JUCE 8.0.4 с нативной поддержкой macOS 15 SDK
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# 2. Signalsmith Stretch (тайм-стретч лупов без изменения тональности)
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/signalsmith-audio/signalsmith-stretch.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(signalsmith_stretch)

juce_add_plugin(MelodicAtlas
    COMPANY_NAME "BorisAudio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE BrAu
    PLUGIN_CODE MlAt
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Melodic-Atlas"
)

juce_generate_juce_header(MelodicAtlas)

target_sources(MelodicAtlas
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(MelodicAtlas
    PRIVATE
        Source
        ${signalsmith_stretch_SOURCE_DIR}
)

target_compile_definitions(MelodicAtlas
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(MelodicAtlas
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_opengl
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
