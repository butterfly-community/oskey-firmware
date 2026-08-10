# Lichuang ESP32-S3 SD card

## Build configuration

Enable the SD card together with the display:

```conf
CONFIG_OSKEY_DISPLAY=y
CONFIG_OSKEY_SD_CARD=y
```

The standard Lichuang build profiles in `build.ts` enable both options. When
`CONFIG_OSKEY_SD_CARD` is disabled, the SD card application module and LVGL
file-browser page are not compiled and the **Files** item is absent from Device
settings.

`CONFIG_OSKEY_SD_CARD` selects Zephyr's ESP32 SDHC stack, SDMMC disk layer and
FatFs implementation. It also enables long filenames and compiles FatFs in
read-only mode. Automatic formatting is disabled, so the firmware never
creates, modifies or repairs a filesystem.

## Hardware

The board connects its TF card socket to the ESP32-S3 SDMMC peripheral in
1-bit mode:

| Signal | GPIO |
| --- | ---: |
| CLK | 47 |
| CMD | 48 |
| D0 | 21 |

The slot and pin routing are defined in `boards/esp32s3_lichuang.overlay` and
run at up to 20 MHz. They use Zephyr's upstream `sdhc_esp32`, SDMMC disk and
FatFs drivers; the project does not carry a board-specific SD driver.

## File browser

Open **Device settings > Files** to mount the card. The browser supports:

- FAT16 and FAT32 filesystems with long filenames;
- root and nested directory navigation;
- file-size display;
- 16 entries per page to bound LVGL memory use;
- retry after a missing, replaced or unreadable card.

The card is mounted only while the Files page is active and is unmounted when
the page is closed. The socket has no card-detect signal available to the
application, so insertion and removal are detected by filesystem operations.
Use **Retry** after changing the card.

The browser does not open, create, rename, copy or delete files. exFAT is not
enabled in the initial implementation. Format cards as FAT32 on a computer
before use.

The SD card is independent of the internal ZMS settings storage. Removing or
reformatting it cannot erase the wallet or application settings.
