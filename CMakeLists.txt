cmake_minimum_required(VERSION 3.22)
project(EpsilonBounce VERSION 1.3.0 LANGUAGES C CXX)

# macOS: 12.0 target and JUCE 9.0.5 (bundled in ./JUCE in the Projucer package; CI clones it).
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(JUCE)
set(bounce_formats VST3 Standalone)
if(APPLE)
    list(APPEND bounce_formats AU)
endif()

juce_add_plugin(EpsilonBounce
    COMPANY_NAME "Epsilon Sounds"
    BUNDLE_ID com.epsilonsounds.bounce
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE EpsS
    PLUGIN_CODE EpBn
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS ${bounce_formats}
    PRODUCT_NAME "Epsilon Bounce"
    ICON_BIG Resources/AppIcon.png
    DESCRIPTION "Re-grooves audio loops to dembow, drill and trap pockets"
)
juce_generate_juce_header(EpsilonBounce)
juce_add_binary_data(BounceAssets SOURCES
    Resources/EpsilonDial-Regular.ttf
    Resources/EpsilonDial-Bold.ttf
)
set_target_properties(BounceAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(EpsilonBounce PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_include_directories(EpsilonBounce PRIVATE Source Shared/epsilon_ui)
target_compile_features(EpsilonBounce PRIVATE cxx_std_17)
target_compile_definitions(EpsilonBounce PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(EpsilonBounce PRIVATE
    BounceAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Engine tests (no JUCE needed): cmake --build build --target BounceEngineTests && ctest
option(BOUNCE_BUILD_TESTS "Build the engine tests" ON)
if(BOUNCE_BUILD_TESTS)
    enable_testing()
    add_executable(BounceEngineTests Tests/EngineTests.cpp)
    target_include_directories(BounceEngineTests PRIVATE Source)
    target_compile_features(BounceEngineTests PRIVATE cxx_std_17)
    add_test(NAME BounceEngineTests COMMAND BounceEngineTests)
endif()

# Headless host test + editor snapshot. Off by default:
#   cmake -DBOUNCE_BUILD_BENCH=ON ... && cmake --build build --target BounceBench
option(BOUNCE_BUILD_BENCH "Build the host test / snapshot bench" OFF)
if(BOUNCE_BUILD_BENCH)
    juce_add_console_app(BounceBench PRODUCT_NAME "BounceBench")
    target_sources(BounceBench PRIVATE Tests/BounceBench.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    juce_generate_juce_header(BounceBench)
    target_include_directories(BounceBench PRIVATE Source Shared/epsilon_ui)
    target_compile_features(BounceBench PRIVATE cxx_std_17)
    target_compile_definitions(BounceBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Name="Epsilon Bounce")
    target_link_libraries(BounceBench PRIVATE BounceAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
