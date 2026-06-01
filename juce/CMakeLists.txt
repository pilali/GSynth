# GSynth — JUCE build (VST3 + AU + Standalone), sharing the LV2 DSP core.
#
# Configure & build:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# JUCE is fetched automatically at the tag below. To build against a local
# checkout instead (faster, offline):
#   cmake -B build -DGSYNTH_JUCE_DIR=/path/to/JUCE
#
# Override the fetched version with -DGSYNTH_JUCE_TAG=<tag>.

cmake_minimum_required(VERSION 3.22)

# macOS: universal (Apple Silicon + Intel) binaries by default, min version 10.13.
# Must be set before project(). Ignored on other platforms; override on the CLI.
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS target architectures")
endif()
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(GSynth VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Obtain JUCE
# ---------------------------------------------------------------------------
set(GSYNTH_JUCE_TAG "8.0.8" CACHE STRING "JUCE git tag to fetch when GSYNTH_JUCE_DIR is unset")

# Install the built plug-ins into the user plug-in folders so hosts and auval
# pick them up immediately (AU -> ~/Library/Audio/Plug-Ins/Components, etc.).
# Turn off for CI/packaging with -DGSYNTH_COPY_AFTER_BUILD=OFF.
option(GSYNTH_COPY_AFTER_BUILD "Copy plug-ins to the user plug-in folders after build" ON)

if(DEFINED GSYNTH_JUCE_DIR)
    message(STATUS "GSynth: using local JUCE at ${GSYNTH_JUCE_DIR}")
    add_subdirectory(${GSYNTH_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    message(STATUS "GSynth: fetching JUCE ${GSYNTH_JUCE_TAG} via FetchContent")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${GSYNTH_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(GSynth
    COMPANY_NAME              "pilali"
    BUNDLE_ID                 com.pilali.gsynth
    PLUGIN_MANUFACTURER_CODE  Plli
    PLUGIN_CODE               Gsyn
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   ${GSYNTH_COPY_AFTER_BUILD}
    FORMATS                   AU VST3 Standalone
    VST3_CATEGORIES           Fx Filter
    AU_MAIN_TYPE              kAudioUnitType_Effect
    PRODUCT_NAME              "GSynth")

# Shared DSP core (the very same C file the LV2 plug-in compiles).
set(GSYNTH_DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_sources(GSynth PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    ${GSYNTH_DSP_DIR}/gsynth_dsp.c)

target_include_directories(GSynth PRIVATE ${GSYNTH_DSP_DIR})

target_compile_definitions(GSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(GSynth
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
