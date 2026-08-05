# Firmware Update

OSKey uses MCUboot test swaps with signed P-256 images. A new image is confirmed only after the
application finishes startup successfully, and MCUboot rejects older versions.

The bootloader and application use the board's default A/B flash layout.

Build the bootloader and application together:

```sh
west build --sysbuild -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  -- \
  -DCONFIG_OSKEY_MCUBOOT=y \
  -DCONFIG_OSKEY_BLUETOOTH=y \
  -DEXTRA_DTC_OVERLAY_FILE="boards/esp32s3_lichuang.overlay;boards/overlay/mcumgr_uart0.overlay"
```

UART updates are enabled for one boot by the OSKey `FirmwareUpdateRequest`; the normal wallet UART
is not started in that mode. Bluetooth updates use the authenticated SMP service while the normal
application is running.

Use `mcumgr` over UART. For Bluetooth on Linux, use `smpclient` through BlueZ so the client reuses
the authenticated bond. Upload `ohw-nano/zephyr/zephyr.signed.bin`, never the unsigned binary.
