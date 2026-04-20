juce_add_plugin(Lemonade
    COMPANY_NAME "1017 DSP"
    BUNDLE_ID "com.1017dsp.lemonade"
    PLUGIN_MANUFACTURER_CODE TZSX
    PLUGIN_CODE LMND
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "LEMONADE"
    PLUGIN_NAME "LEMONADE"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_sources(Lemonade PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(Lemonade PRIVATE
    Source
    ${CMAKE_SOURCE_DIR}/Shared
)

target_compile_definitions(Lemonade PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Lemonade PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_graphics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
