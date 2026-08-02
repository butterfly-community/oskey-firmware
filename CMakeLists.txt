cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oskey)

include(cmake/lvgl.cmake)

target_sources(app PRIVATE
  src/main.c
  src/bus.c
  src/uart.c
  src/bluetooth/bluetooth.c
  src/storage.c
  src/boot.c
  src/net/wifi.c
  src/net/mqtt.c
  src/gpio.c
  src/usb/webusb.c
  src/app.c
  src/display/display.c
)

target_sources_ifdef(CONFIG_OSKEY_RUST app PRIVATE
  src/core.c
  src/transport.c
)

target_sources_ifdef(CONFIG_OSKEY_DISPLAY app PRIVATE
  src/display/ui.c
  src/display/ui_status.c
  src/display/ui_dialog.c
  src/display/ui_pages.c
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

if(CONFIG_OSKEY_RUST AND CONFIG_TF_PSA_CRYPTO_BUILTIN AND
   CONFIG_TF_PSA_CRYPTO_USER_CONFIG_FILE STREQUAL "oskey_tf_psa_config.h")
  zephyr_include_directories(src/crypto)
  target_sources(app PRIVATE src/crypto/threading.c)
endif()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(cmake/rust.cmake)
