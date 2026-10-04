# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oskey_showcase)

# Compile the product UI against deterministic presentation backends.
# This application is separate from the firmware build.
get_filename_component(OSKEY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_include_directories(app PRIVATE ${OSKEY_SOURCE} ${OSKEY_SOURCE}/display)
target_sources(app PRIVATE
  main.c fixtures.c
  ${OSKEY_SOURCE}/display/ui.c
  ${OSKEY_SOURCE}/display/ui_status.c
  ${OSKEY_SOURCE}/display/ui_dialog.c
  ${OSKEY_SOURCE}/display/ui_pages.c
  ${OSKEY_SOURCE}/display/ui_hw.c
  ${OSKEY_SOURCE}/display/ui_entropy.c
  ${OSKEY_SOURCE}/display/ui_confirmation.c
  ${OSKEY_SOURCE}/display/ui_qr.c
  ${OSKEY_SOURCE}/display/ui_files.c
  ${OSKEY_SOURCE}/display/assets/assets.c
)
target_compile_definitions(app PRIVATE
  CONFIG_OSKEY_DISPLAY=1 CONFIG_OSKEY_RUST=1 CONFIG_OSKEY_MESSAGE_BUS=1
  CONFIG_OSKEY_NXP_SE=1 CONFIG_OSKEY_FIDO2=1 CONFIG_OSKEY_USB=1
  CONFIG_OSKEY_WIFI=1 CONFIG_OSKEY_BLUETOOTH=1 CONFIG_OSKEY_CAMERA=1
  CONFIG_OSKEY_QR_SCANNER=1 CONFIG_OSKEY_AUDIO=1 CONFIG_OSKEY_MICROPHONE=1
  CONFIG_OSKEY_IMU=1 CONFIG_OSKEY_REMOVABLE_MEDIA=1
  CONFIG_OSKEY_IMU_RENDER_INTERVAL_MS=100 CONFIG_OSKEY_QR_MAX_PAYLOAD_SIZE=2048
  CONFIG_VIDEO_BUFFER_POOL_ALIGN=4
  CONFIG_OSKEY_WIFI_AP_SSID="OSKey-AP"
  CONFIG_OSKEY_WIFI_AP_IP_ADDRESS="192.168.4.1"
  CONFIG_OSKEY_WIFI_AP_NETMASK="255.255.255.0"
)

file(GLOB display_assets CONFIGURE_DEPENDS "${OSKEY_SOURCE}/display/assets/generated/*.a8")
foreach(asset IN LISTS display_assets)
  get_filename_component(name "${asset}" NAME_WE)
  generate_inc_file_for_target(app "${asset}"
    "${ZEPHYR_BINARY_DIR}/include/generated/oskey_${name}.a8.inc")
endforeach()
