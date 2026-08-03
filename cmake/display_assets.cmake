find_program(SVG_RENDERER gdk-pixbuf-thumbnailer)
find_program(MAGICK_EXECUTABLE magick)
if(NOT SVG_RENDERER OR NOT MAGICK_EXECUTABLE)
  message(STATUS "Display asset tools not found; using committed display assets")
  return()
endif()

file(MAKE_DIRECTORY "${ASSET_OUTPUT_DIR}" "${ASSET_TEMP_DIR}")

function(generate_asset output_name source_name size)
  set(source "${ASSET_SOURCE_DIR}/${source_name}.svg")
  set(png "${ASSET_TEMP_DIR}/${output_name}.png")
  set(temporary "${ASSET_TEMP_DIR}/${output_name}.a8")
  set(output "${ASSET_OUTPUT_DIR}/${output_name}.a8")

  execute_process(
    COMMAND "${SVG_RENDERER}" -s ${size} "${source}" "${png}"
    COMMAND_ERROR_IS_FATAL ANY
  )
  execute_process(
    COMMAND "${MAGICK_EXECUTABLE}"
      "${png}"
      -alpha extract
      -depth 8
      "gray:${temporary}"
    COMMAND_ERROR_IS_FATAL ANY
  )

  file(SIZE "${temporary}" actual_size)
  math(EXPR expected_size "${size} * ${size}")
  if(NOT actual_size EQUAL expected_size)
    message(FATAL_ERROR
      "Invalid ${output_name} asset size: expected ${expected_size}, got ${actual_size}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${temporary}" "${output}"
    COMMAND_ERROR_IS_FATAL ANY
  )
endfunction()

generate_asset(back back 24)
generate_asset(bluetooth bluetooth 24)
generate_asset(chevron_right chevron_right 24)
generate_asset(document document 24)
generate_asset(ethereum ethereum 24)
generate_asset(eye eye 24)
generate_asset(eye_off eye_off 24)
generate_asset(failure failure 24)
generate_asset(passkey passkey 24)
generate_asset(refresh refresh 24)
generate_asset(settings settings 24)
generate_asset(shuffle shuffle 24)
generate_asset(success success 24)
generate_asset(trash trash 24)
generate_asset(usb usb 24)
generate_asset(wallet wallet 24)
generate_asset(wallet_logo wallet 60)
generate_asset(warning warning 24)
generate_asset(wifi wifi 24)
generate_asset(wifi_ap access_point 24)

message(STATUS "Generated display assets in ${ASSET_OUTPUT_DIR}")
