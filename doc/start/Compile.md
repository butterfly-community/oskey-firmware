You can directly use the Dockerfile, which provides a tested compilation and debugging environment. This project does not recommend native compilation on Windows. Windows users are advised to use WSL.

## Getting Started

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## Try Compile Demo

`esp32c3_devkitm` is our recommended development board. For other boards, please check the [Supported Boards](https://docs.zephyrproject.org/latest/boards/index.html).

If you have completed the steps in the link above, try compiling the example with this command.

```bash
west build -p always -b esp32c3_devkitm samples/hello_world
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

**If use Espressif ESP32/ESPS2/ESPS3 chip，You will need to configure the Espressif rust toolchain**

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
   west build -p always -b esp32c3_devkitm
   ```

4. Flash

   ```bash
   west flash
   ```
