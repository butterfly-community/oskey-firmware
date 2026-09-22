<!-- SPDX-License-Identifier: MPL-2.0 -->

The Dockerfile describes the CI build environment. Use the existing Zephyr workspace for local builds; do not rebuild Docker images locally. This project does not recommend native compilation on Windows. Windows users are advised to use WSL.

## Getting Started

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## Try Compile Demo

`esp32s3_devkitc/esp32s3/procpu` is our recommended development board. OSKey requires native pointer-width atomic operations.

If you have completed the steps in the link above, try compiling the example with this command.

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu samples/hello_world
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

**When using an Xtensa ESP32, ESP32-S2, or ESP32-S3, configure the Espressif Rust toolchain.**

[https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html](https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html)



## Compile OSKey

1. Clone source code

   ```bash
   git clone --recursive https://github.com/butterfly-community/oskey-firmware.git
   ```

2. Set environment variables

   ```bash
   source ~/zephyrproject/zephyr/zephyr-env.sh
   ```

3. Compile OSKey source code

   ```bash
   west build -p always -b esp32s3_devkitc/esp32s3/procpu
   ```

4. Flash

   ```bash
   west flash
   ```
