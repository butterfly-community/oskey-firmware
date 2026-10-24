<!-- SPDX-License-Identifier: MPL-2.0 -->

[中文](compile_zh.md) · [Quick Start](README.md) · [Product Gallery](../showcase/README.md)

# Build OSKey

The Dockerfile describes the CI build environment. Use the existing Zephyr workspace for local builds; do not rebuild Docker images locally. This project does not recommend native compilation on Windows. Windows users are advised to use WSL.

## Getting Started

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## Load the Development Environment

Before building, flashing, or running any `west` command, load all three environments in the same shell. These paths describe the existing local workspace; adjust them to your installation:

```sh
source ~/Develop/platform/zephyr-project/.venv/bin/activate
source ~/Develop/platform/zephyr-project/zephyr/zephyr-env.sh
source ~/.esp.sh

command -v west
test -n "$ZEPHYR_BASE"
rustc +esp --version
```

The virtual environment provides `west` and Python dependencies, `zephyr-env.sh` exposes the Zephyr workspace, and `.esp.sh` loads the esp-rs toolchain for Xtensa targets. Repeat this setup in each new shell.

## Try Compile Demo

`esp32s3_devkitc/esp32s3/procpu` is our recommended development board. OSKey requires native pointer-width atomic operations.

If you have completed the steps in the link above, try compiling the example with this command.

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu \
  -d build/hello-world "$ZEPHYR_BASE/samples/hello_world"
```
## Rust Support

**This project also requires additional Rust configuration. please refer here.**

[https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)

[https://docs.zephyrproject.org/latest/develop/languages/rust/index.html](https://docs.zephyrproject.org/latest/develop/languages/rust/index.html)

Use the project's [`west.yml`](../../west.yml) and follow the
[workspace setup instructions](../../patch/README.md#reproduce-the-workspace).
They pin Zephyr main and Rust to validated commits and apply all required patches.
Run the patch helper from the OSKey source directory:

```bash
python3 patch/apply.py /path/to/zephyr-project
```

Also refer to [Docker](../../Dockerfile)

**Xtensa ESP32, ESP32-S2, and ESP32-S3 targets require the Espressif Rust toolchain loaded above.**

[https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html](https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html)



## Compile OSKey

1. Clone source code

   ```bash
   git clone --recursive https://github.com/butterfly-community/oskey-firmware.git
   cd oskey-firmware
   ```

2. Load the development environment shown above in this shell, and complete the pinned workspace setup linked in Rust Support.

3. Compile OSKey source code

   ```bash
   west build -p always -b esp32s3_devkitc/esp32s3/procpu \
     -d build/esp32s3 -- -DCONFIG_OSKEY_STORAGE=y
   ```

4. Flash

   ```bash
   west flash -d build/esp32s3
   ```

This profile enables persistent storage and UART wallet communication. For the Lichuang display and peripherals, use the configuration below. Other board profiles and their overlays are listed in [`build.ts`](../../build.ts).

## Lichuang ESP32-S3 Display and Peripherals

Run from the OSKey source directory after loading the same environments:

```sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu \
  -d build/lichuang \
  -S espressif-flash-16M -S espressif-psram-8M -- \
  -DCONFIG_OSKEY_STORAGE=y -DCONFIG_OSKEY_DISPLAY=y \
  -DCONFIG_OSKEY_ESP_SPIRAM=y -DCONFIG_OSKEY_CAMERA=y \
  -DCONFIG_OSKEY_QR_SCANNER=y -DCONFIG_OSKEY_AUDIO=y \
  -DCONFIG_OSKEY_IMU=y -DCONFIG_OSKEY_REMOVABLE_MEDIA=y \
  -DEXTRA_CONF_FILE=boards/esp32s3_lichuang.conf \
  -DEXTRA_DTC_OVERLAY_FILE=boards/esp32s3_lichuang.overlay
west flash -d build/lichuang
```

Bluetooth, Wi-Fi, USB, FIDO2, the NXP secure element, and MCUboot updates are selected separately. See the [NXP guide](../security/nxp-a5000.md), [signed firmware update guide](firmware-update.md), and [Linux simulator guide](linux-development.md) for those configurations.

## Logging and Documentation Images

Logging and the boot banner are disabled by default. Add `-DCONFIG_OSKEY_LOG=y` after `--` in the build command before flashing and attaching a serial monitor.

Current documentation images are in [`docs/showcase/`](../showcase/README.md). The [capture tool instructions](../../src/tests/showcase/README.md) explain how to regenerate and verify them using the existing Zephyr environment.
