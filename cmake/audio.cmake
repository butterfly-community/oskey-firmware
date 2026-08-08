# Audio asset generation, mirroring the display asset pipeline
# (see cmake/display_assets.cmake). The WAV asset is converted into a C
# byte-array include under src/audio/assets/generated/.
set(AUDIO_BEEP_INC ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/assets/generated/beep.wav.inc)

add_custom_command(
  OUTPUT ${AUDIO_BEEP_INC}
  COMMAND ${CMAKE_COMMAND}
    "-DASSET_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/audio/assets"
    "-DASSET_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/audio/assets/generated"
    "-DASSET_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/audio-assets"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_assets.cmake"
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/assets/beep.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_assets.cmake
  VERBATIM
)

add_custom_target(audio-assets DEPENDS ${AUDIO_BEEP_INC})
