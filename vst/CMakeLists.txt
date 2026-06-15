cmake_minimum_required(VERSION 3.22)

project(ScratchVST VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS / Apple Silicon. AU is the primary format: VST3 normalises incoming MIDI
# CC into plugin *parameters*, which destroys the SC1000's relative jog stream
# (see docs/ARCHITECTURE.md "Plugin format — build AU, never VST3").
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.13
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ScratchVST
  PRODUCT_NAME              "Scratch VST"
  COMPANY_NAME             "scratch-vst"
  BUNDLE_ID                "com.scratchvst.scratchvst"
  PLUGIN_MANUFACTURER_CODE  Scvt
  PLUGIN_CODE               Scr1
  FORMATS                   AU Standalone
  IS_SYNTH                  TRUE          # instrument: receives MIDI, makes audio from a sample
  NEEDS_MIDI_INPUT          TRUE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  AU_MAIN_TYPE              kAudioUnitType_MusicDevice
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD   TRUE          # installs the .component to ~/Library/Audio/Plug-Ins/Components
)

target_sources(ScratchVST PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
)

target_compile_definitions(ScratchVST PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_FLAC=1
  JUCE_USE_OGGVORBIS=1
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ScratchVST
  PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---- Dev aid: headless GUI snapshot (renders the editor to a PNG) ----
juce_add_console_app(ScratchShot PRODUCT_NAME "ScratchShot")
target_sources(ScratchShot PRIVATE
  tools/snapshot.cpp
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
)
target_compile_definitions(ScratchShot PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_FLAC=1
  JUCE_USE_OGGVORBIS=1
)
target_link_libraries(ScratchShot
  PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
