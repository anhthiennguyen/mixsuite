cmake_minimum_required(VERSION 3.22)
project(MixSuite VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory($ENV{HOME}/JUCE JUCE_build)

juce_add_plugin(MixSuite
    COMPANY_NAME            "anhthientbd"
    PLUGIN_MANUFACTURER_CODE Mxst
    PLUGIN_CODE             Mxst
    FORMATS                 VST3 AU
    PRODUCT_NAME            "MixSuite"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         "Fx" "EQ" "Spatial"
    AU_MAIN_TYPE            kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(MixSuite)

target_sources(MixSuite PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EQComponent.cpp
    Source/SharedAnalyserState.cpp
    Source/TrackState.cpp
    Source/DSPEngine.cpp
    Source/SharedMixerState.cpp
    Source/CanvasComponent.cpp
    Source/SpectrumComponent.cpp
    Source/InstanceListComponent.cpp
)

target_compile_definitions(MixSuite PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(MixSuite PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_graphics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
