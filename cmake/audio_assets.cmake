# SPDX-License-Identifier: MPL-2.0

# Generates a C byte-array include from the raw WAV asset, mirroring the
# display asset pipeline (see cmake/display_assets.cmake).

if(NOT DEFINED ASSET_SOURCE_DIR OR NOT DEFINED ASSET_OUTPUT_DIR OR NOT DEFINED ASSET_TEMP_DIR)
  message(FATAL_ERROR
    "audio_assets.cmake requires ASSET_SOURCE_DIR, ASSET_OUTPUT_DIR and ASSET_TEMP_DIR")
endif()

file(MAKE_DIRECTORY "${ASSET_OUTPUT_DIR}" "${ASSET_TEMP_DIR}")

set(source "${ASSET_SOURCE_DIR}/beep.wav")
set(temporary "${ASSET_TEMP_DIR}/beep.wav.inc")
set(output "${ASSET_OUTPUT_DIR}/beep.wav.inc")

file(SIZE "${source}" source_size)
if(source_size EQUAL 0)
  message(FATAL_ERROR "Empty audio asset: ${source}")
endif()

file(READ "${source}" data HEX)
string(LENGTH "${data}" hex_len)

set(contents "")
set(i 0)
while(i LESS hex_len)
  string(SUBSTRING "${data}" ${i} 16 chunk)
  set(line "")
  set(j 0)
  while(j LESS 16)
    string(SUBSTRING "${chunk}" ${j} 2 byte)
    if(NOT byte)
      break()
    endif()
    string(APPEND line "0x${byte}, ")
    math(EXPR j "${j} + 2")
  endwhile()
  string(APPEND contents "${line}\n")
  math(EXPR i "${i} + 16")
endwhile()

file(WRITE "${temporary}" "${contents}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${temporary}" "${output}"
  COMMAND_ERROR_IS_FATAL ANY
)
message(STATUS "Generated audio asset: ${output}")
