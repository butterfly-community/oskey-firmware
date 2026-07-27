You can directly use the Dockerfile, which provides a tested compilation and debugging environment. This project does not recommend native compilation on Windows. Windows users are advised to use WSL.

## Getting Started

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## Try Compile Demo

`esp32s3_devkitc/esp32s3/procpu` is our recommended development board. OSKey requires native pointer-width atomic operations; targets such as ESP32-C2/C3 and RP2040 are not supported.

If you have completed the steps in the link above, try compiling the example with this command.

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu samples/hello_world
```
## Rust Support

**This project also requires additional Rust configuration. please refer here.**

[https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)

[https://docs.zephyrproject.org/latest/develop/languages/rust/index.html](https://docs.zephyrproject.org/latest/develop/languages/rust/index.html)

Additional application patches are also required.

```bash
cd <YOUR_ZEPHYR_PATH>/modules/lang/rust

wget https://raw.githubusercontent.com/butterfly-community/oskey-firmware/refs/heads/master/patch/rust.patch

git apply rust.patch
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
