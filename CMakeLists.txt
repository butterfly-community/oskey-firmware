cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oskey)

if(CONFIG_OSKEY_FIDO2 AND CONFIG_NATIVE_LIBC)
  zephyr_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

include(cmake/lvgl.cmake)

target_sources(app PRIVATE
  src/main.c
  src/bus.c
  src/uart.c
  src/bluetooth/bluetooth.c
  src/storage.c
  src/net/wifi.c
  src/net/mqtt.c
  src/gpio.c
  src/usb/webusb.c
  src/app.c
  src/display/display.c
)

target_sources_ifdef(CONFIG_OSKEY_MCUBOOT app PRIVATE
  src/boot.c
)

target_sources_ifdef(CONFIG_OSKEY_RUST app PRIVATE
  src/core.c
)

target_sources_ifdef(CONFIG_OSKEY_TEST_FIRMWARE app PRIVATE
  src/test.c
)

target_sources_ifdef(CONFIG_OSKEY_CAMERA app PRIVATE
  src/camera/gc0308.c
  src/camera/camera.c
)

target_sources_ifdef(CONFIG_OSKEY_CAMERA_TEST app PRIVATE
  src/camera/camera_test.c
)

target_sources_ifdef(CONFIG_OSKEY_DISPLAY app PRIVATE
  src/display/ui.c
  src/display/ui_controller.c
  src/display/ui_status.c
  src/display/ui_dialog.c
  src/display/ui_pages.c
  src/display/ui_hw.c
  src/display/ui_confirmation.c
  src/display/assets/assets.c
)

target_sources_ifdef(CONFIG_OSKEY_WIFI app PRIVATE
  src/net/http.c
  src/net/wifi_portal.c
)

include(cmake/wifi.cmake)

target_sources_ifdef(CONFIG_OSKEY_USB app PRIVATE
  src/usb/bulk.c
  src/usb/init.c
)

target_sources_ifdef(CONFIG_OSKEY_FIDO2 app PRIVATE
  src/usb/fido2.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(cmake/rust.cmake)
