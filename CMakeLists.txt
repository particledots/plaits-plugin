cmake_minimum_required(VERSION 3.22)
project(PlaitsPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: AU + VST3 (+ Standalone for quick testing).
set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

# Unlike Rings (which needs audio in, so it is a MIDI-controlled effect), Plaits has no audio input:
# it is a plain instrument, so it shows up in Logic's normal Instrument slot.
juce_add_plugin(Plaits
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Plai
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Plaits"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE)

# --- DSP core: vendored Plaits/stmlib code + our JUCE-free wrapper, built with plain flags ---
set(PLAITS_DSP third_party/plaits/dsp)
add_library(plaits_core STATIC
  src/PlaitsEngine.cpp
  ${PLAITS_DSP}/voice.cc
  ${PLAITS_DSP}/chords/chord_bank.cc
  ${PLAITS_DSP}/engine/additive_engine.cc
  ${PLAITS_DSP}/engine/bass_drum_engine.cc
  ${PLAITS_DSP}/engine/chord_engine.cc
  ${PLAITS_DSP}/engine/fm_engine.cc
  ${PLAITS_DSP}/engine/grain_engine.cc
  ${PLAITS_DSP}/engine/hi_hat_engine.cc
  ${PLAITS_DSP}/engine/modal_engine.cc
  ${PLAITS_DSP}/engine/noise_engine.cc
  ${PLAITS_DSP}/engine/particle_engine.cc
  ${PLAITS_DSP}/engine/snare_drum_engine.cc
  ${PLAITS_DSP}/engine/speech_engine.cc
  ${PLAITS_DSP}/engine/string_engine.cc
  ${PLAITS_DSP}/engine/swarm_engine.cc
  ${PLAITS_DSP}/engine/virtual_analog_engine.cc
  ${PLAITS_DSP}/engine/waveshaping_engine.cc
  ${PLAITS_DSP}/engine/wavetable_engine.cc
  ${PLAITS_DSP}/engine2/chiptune_engine.cc
  ${PLAITS_DSP}/engine2/phase_distortion_engine.cc
  ${PLAITS_DSP}/engine2/six_op_engine.cc
  ${PLAITS_DSP}/engine2/string_machine_engine.cc
  ${PLAITS_DSP}/engine2/virtual_analog_vcf_engine.cc
  ${PLAITS_DSP}/engine2/wave_terrain_engine.cc
  ${PLAITS_DSP}/fm/algorithms.cc
  ${PLAITS_DSP}/fm/dx_units.cc
  ${PLAITS_DSP}/physical_modelling/modal_voice.cc
  ${PLAITS_DSP}/physical_modelling/resonator.cc
  ${PLAITS_DSP}/physical_modelling/string.cc
  ${PLAITS_DSP}/physical_modelling/string_voice.cc
  ${PLAITS_DSP}/speech/lpc_speech_synth.cc
  ${PLAITS_DSP}/speech/lpc_speech_synth_controller.cc
  ${PLAITS_DSP}/speech/lpc_speech_synth_phonemes.cc
  ${PLAITS_DSP}/speech/lpc_speech_synth_words.cc
  ${PLAITS_DSP}/speech/naive_speech_synth.cc
  ${PLAITS_DSP}/speech/sam_speech_synth.cc
  third_party/plaits/resources.cc
  third_party/stmlib/utils/random.cc
  third_party/stmlib/dsp/units.cc)
target_include_directories(plaits_core PUBLIC src third_party)
# TEST=1 is the upstream desktop-build switch: it turns the STM32 flash / assembly code into portable stand-ins.
target_compile_definitions(plaits_core PRIVATE TEST=1)
set_target_properties(plaits_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Some upstream headers rely on <cstdio>/<cstdlib>/<cmath>/<cstdint> being pulled in transitively.
if(MSVC)
  target_compile_options(plaits_core PRIVATE /FIcstdio /FIcstdlib /FIcmath /FIcstdint)
else()
  target_compile_options(plaits_core PRIVATE "SHELL:-include cstdio" "SHELL:-include cstdlib" "SHELL:-include cmath" "SHELL:-include cstdint" -Wno-unused-local-typedef -Wno-unused-variable)
endif()

# --- The plugin ---
target_sources(Plaits PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_compile_definitions(Plaits PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Plaits PRIVATE
  plaits_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Headless test of the DSP wrapper:  cmake --build build --target engine_test && build/engine_test ---
add_executable(engine_test EXCLUDE_FROM_ALL src/engine_test.cpp)
target_link_libraries(engine_test PRIVATE plaits_core)

# --- Engine-switching regression test:  JUNK=0xFF build/sweep_test ---
add_executable(sweep_test EXCLUDE_FROM_ALL src/sweep_test.cpp)
target_link_libraries(sweep_test PRIVATE plaits_core)
