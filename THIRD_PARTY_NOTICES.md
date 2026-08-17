<!-- SPDX-License-Identifier: MPL-2.0 -->

# Third-party notices

The repository's original OSKey source and documentation are licensed under
the Mozilla Public License 2.0 as stated in `LICENSE`. Files carrying another
SPDX identifier, and bundled dependencies with their own license files, remain
under those terms.

## Apache-2.0 material

The following material is derived from Apache-2.0 upstream projects. Modified
source files retain the upstream copyright notices and identify the OSKey
changes in their file headers. A copy of the license is available at
`LICENSES/Apache-2.0.txt`.

### Zephyr Project

- `src/net/wifi.c`
- `src/usb/bulk.c`, `src/usb/init.c`, `src/usb/msosv2.h`, and
  `src/usb/webusb.c`
- Zephyr-targeting patches under `patch/`
- `docs/image/board/disco_l475_iot1.jpg`,
  `docs/image/board/frdm_k64f.jpg`,
  `docs/image/board/nucleo_f401re.jpg`, and
  `docs/image/board/stm32h747i_disco.jpg`

### Espressif

- `drivers/audio/es7210.c` is adapted from the
  [`espressif/es7210` component, version 1.0.0](https://components.espressif.com/components/espressif/es7210/versions/1.0.0).
- `drivers/audio/es8311.c` is adapted from the
  [`espressif/es8311` component, version 0.0.2](https://components.espressif.com/components/espressif/es8311/versions/0.0.2).
- `drivers/video/gc0308.c` and `drivers/video/gc0308_settings.h` are adapted
  from the `esp32-camera` GC0308 sensor driver.

`patch/tf-psa-threading.patch` modifies Apache-2.0 material from the Mbed TLS
and TF-PSA-Crypto integration.

## Tabler Icons

SVG icons and their generated display assets under `src/display/assets/` are
based on Tabler Icons and are distributed under the MIT License. The applicable
copyright notice and license text are in `src/display/assets/LICENSE.tabler`.

## Product images and marks

Product photographs, project logos, and service marks under `docs/image/` are
included for identification and documentation. In particular, GCC, OpenBuild,
board manufacturer, Telegram, and WeChat names and marks remain the property of
their respective owners. The repository's MPL-2.0 license does not grant rights
to those third-party marks. See `docs/image/README.md` for the file-level media
classification, including historical files whose original source or licensing
details may no longer be known to OSKey.

Demo captures, setup screenshots, OSKey community QR codes, and other original
OSKey documentation media remain covered by the repository license.

## Bundled libraries

Libraries under `lib/` are separate components. Their own license files and
per-file notices define the applicable terms; the repository-level MPL-2.0
license does not replace them.
