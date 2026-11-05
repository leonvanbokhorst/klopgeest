cmake_minimum_required(VERSION 3.22)

project(KLOPGEEST VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# JUCE. A local checkout (.deps/JUCE via scripts/fetch-juce.sh) keeps builds
# reproducible and network-free. FetchContent is the fallback.
# -----------------------------------------------------------------------------
set(KLOPGEEST_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/JUCE" CACHE PATH "Path to a local JUCE checkout")
if (EXISTS "${KLOPGEEST_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "KLOPGEEST: using local JUCE at ${KLOPGEEST_JUCE_DIR}")
    add_subdirectory("${KLOPGEEST_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
else()
    message(STATUS "KLOPGEEST: fetching JUCE 9.0.5")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# -----------------------------------------------------------------------------
# Sources. Globbed so parallel workstreams can add files without editing this
# file; CONFIGURE_DEPENDS makes the build re-glob automatically.
# -----------------------------------------------------------------------------
file(GLOB_RECURSE KLOPGEEST_CORE     CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB_RECURSE KLOPGEEST_DSP      CONFIGURE_DEPENDS src/dsp/*.cpp)
file(GLOB_RECURSE KLOPGEEST_SEQ      CONFIGURE_DEPENDS src/sequencer/*.cpp)
file(GLOB_RECURSE KLOPGEEST_UI       CONFIGURE_DEPENDS src/ui/*.cpp)
file(GLOB_RECURSE KLOPGEEST_PLUGIN   CONFIGURE_DEPENDS src/plugin/*.cpp)
file(GLOB_RECURSE KLOPGEEST_TESTS    CONFIGURE_DEPENDS tests/*.cpp)

set(KLOPGEEST_SOURCES ${KLOPGEEST_CORE} ${KLOPGEEST_DSP} ${KLOPGEEST_SEQ}
                      ${KLOPGEEST_UI} ${KLOPGEEST_PLUGIN})

set(KLOPGEEST_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# -----------------------------------------------------------------------------
# The instrument: AU / VST3 / Standalone.
# -----------------------------------------------------------------------------
juce_add_plugin(KLOPGEEST
    PRODUCT_NAME                "KLOPGEEST"
    COMPANY_NAME                "Master Lonn & Little Wan"
    BUNDLE_ID                   "com.masterlonn.littlewan.klopgeest"
    PLUGIN_MANUFACTURER_CODE    MLLW
    PLUGIN_CODE                 Klpg
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_CATEGORY             "Instrument"
    VST3_CATEGORIES             "Instrument"
    COMPANY_COPYRIGHT           "Copyright 2026 Master Lonn & Little Wan"
    VERSION                     ${PROJECT_VERSION})

juce_generate_juce_header(KLOPGEEST)

target_sources(KLOPGEEST PRIVATE ${KLOPGEEST_SOURCES})
target_include_directories(KLOPGEEST PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(KLOPGEEST PRIVATE ${KLOPGEEST_DEFS})
target_link_libraries(KLOPGEEST
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Headless tool: tests + demo render + offscreen UI snapshot. It compiles the
# exact same sources as the plug-in, so what we test is what ships.
# -----------------------------------------------------------------------------
option(KLOPGEEST_BUILD_TOOL "Build the headless test/render tool" ON)

if (KLOPGEEST_BUILD_TOOL)
    file(GLOB_RECURSE KLOPGEEST_TOOL_SRC CONFIGURE_DEPENDS tools/*.cpp)
    juce_add_console_app(klop_tool PRODUCT_NAME "klop-tool")
    target_sources(klop_tool PRIVATE ${KLOPGEEST_TOOL_SRC} ${KLOPGEEST_SOURCES} ${KLOPGEEST_TESTS})
    target_include_directories(klop_tool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(klop_tool PRIVATE ${KLOPGEEST_DEFS} KLOPGEEST_HEADLESS=1)
    target_link_libraries(klop_tool
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_gui_basics)
endif()
