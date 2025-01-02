[中文点这里](./README_zh.md)

## What is OHW?

OHW (Open Hardware Wallet) is a fully open-source, non-commercial hardware wallet project. Our mission is to help you create your own hardware wallet, both in terms of software and hardware.

Unlike commercial hardware products where open-source is used to drive sales of their commercial products, our open-source focus is on building infrastructure.

We don't restrict users to specific chip manufacturers or models. Users have the freedom to choose from over 200 chips from more than 10 manufacturers, and can work with over 3000 development boards made by chip manufacturers or third parties. For example, popular open hardware platforms like Arduino or Raspberry Pi.

Users can also create their own development boards based on our reference designs, which we will soon release.

The Open Hardware Wallet supports multiple hardware architectures and is optimized for resource-constrained devices with security built-in. The cheapest supported MCU costs only $0.3, with optional support for Bluetooth, WiFi, and display capabilities.

## What can this product do?

We are building core infrastructure connecting blockchain with physical devices. Not just a hardware wallet.

### Feature:

#### ✅ Mnemonic Generation and Import on chip.

[BIP39](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki) All [unit tests](https://github.com/butterfly-communtiy/ohw-lib-wallets/blob/main/src/mnemonic.rs) completed successfully.

#### ✅ HD (Hierarchical Deterministic) Wallet and Path Derivation on chip.

[BIP32](https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki) All [unit tests](https://github.com/butterfly-communtiy/ohw-lib-wallets/blob/main/src/wallets.rs) completed successfully.

#### ✅ secp256k1.

pubkey, signature and [unit tests](https://github.com/butterfly-communtiy/ohw-lib-wallets/blob/main/src/alg/crypto.rs).

#### 🚧 WebBrowser Support

Currently supports initialization, generation address and signing via WebSerial in browser environments, need help with next phase of development.

#### 🚧 WebUsb WiFi Bluetooth Support.

Need help.

#### 🚧 Display Support.

Need help.

### Demo Video:

[![Open Hardware Wallet - Task 2](https://res.cloudinary.com/marcomontalbano/image/upload/v1735636806/video_to_markdown/images/youtube--q8UIM43psh4-c05b58ac6eb4c4700831b2b3070cd403.jpg)](https://www.youtube.com/watch?v=q8UIM43psh4 "Open Hardware Wallet - Task 2")

## How to Use the Firmware

### Pre-compiled Firmware

  We provide pre-compiled firmware for development boards we own. Please check the [Releases](https://github.com/butterfly-communtiy/ohw-elf-firmware/releases) section on the right or see below for our available development boards.

### Self-compiled Firmware

  If your development board is not included in pre-compiled firmware, please check the following links to set up the development environment and compile firmware for your board.

  [https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

  **If you have completed the steps in the link above, try compiling the example with this command.**

  `west build -p always -b esp32c3_devkitm samples/hello_world`

 **This project also requires additional Rust configuration. please refer here.**

[  https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)

  [https://github.com/zephyrproject-rtos/zephyr-lang-rust/discussions/11#discussioncomment-10905800](https://github.com/zephyrproject-rtos/zephyr-lang-rust/discussions/11#discussioncomment-10905800)

  **Also refer to** **[Dockerfile](./Dockerfile)**


### Compile ohw

* Clone source code

  ```bash
  git clone --recursive https://github.com/butterfly-communtiy/ohw-elf-firmware.git
  ```

* Set environment variables (* Required)

  > Please refer to [here](https://docs.zephyrproject.org/latest/develop/env_vars.html#zephyr-environment-scripts) for the Windows environment.

  ```bash
  # Mac or Linux environment
  export ZEPHYR_BASE= ~/zephyrproject/zephyr
  # Or
  source ~/zephyrproject/zephyr/zephyr-env.sh
  ```

* Compile ohw source code

  ```bash
  west build -p always -b esp32c3_devkitm
  ```

### Development Boards

We carefully selected 5 development boards representing 3 different architectures from 5 different chip manufacturers as our officially supported boards. This demonstrates our vendor-independent capability. Our developers actively develop and test on these boards.

We also provide direct support for over 300 development boards without any modifications needed. For a complete list, please check our [Supported Boards and Shields](https://docs.zephyrproject.org/latest/boards/index.html) documentation.

Due to the wide variety of development board models available, only the chip price is listed here. Please select your preferred development board.

|     Name     | [ESP32-C3-DevKitM-1](https://docs.zephyrproject.org/latest/boards/espressif/esp32c3_devkitm/doc/index.html) 🔥🔥 | [Raspberry Pi Pico](https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_pico/doc/index.html) 🔥 | [Nucleo F401RE](https://docs.zephyrproject.org/latest/boards/st/nucleo_f401re/doc/index.html) | [nRF52840-MDK](https://docs.zephyrproject.org/latest/boards/makerdiary/nrf52840_mdk/doc/index.html) | [NXP FRDM-K64F](https://docs.zephyrproject.org/latest/boards/nxp/frdm_k64f/doc/index.html) |
| :----------: | :-----------------------------------------------------------------------------------------------------------: | :---------------------------------------------------------------------------------------------------: | :----------------------------------------------------------------------------------------: | :----------------------------------------------------------------------------------------------: | :-------------------------------------------------------------------------------------: |
|    Image    |                    ![esp32-c3-devkitm](doc/image/board/esp32-c3-devkitm-1-v1-isometric.png)                    |                               ![rpi-pico](doc/image/board/pico-board.png)                               |                       ![stm32f401](doc/image/board/nucleo_f401re.jpg)                       |                        ![nrf52840-mdk](doc/image/board/mdk52840-cover.png)                        |                        ![frdm_k64f](doc/image/board/frdm_k64f.jpg)                        |
| Manufacturer |                                                   Espressif                                                   |                                             Raspberry Pi                                             |                                     STMicroelectronics                                     |                                       Nordic Semiconductor                                       |                                           NXP                                           |
|     Chip     |                                                  ESP32-C3FH4                                                  |                                                RP2040                                                |                                       STM32F401RET6                                       |                                             nRF52840                                             |                                     MK64FN1M0VLL12                                     |
| Architecture |                                                    RISC-V                                                    |                                             Arm Cortex-M0                                             |                                       ARM Cortex-M4                                       |                                          ARM Cortex-M4                                          |                                      ARM Cortex-M4                                      |
|     RAM     |                                                    400 KB                                                    |                                                264 KB                                                |                                           96 KB                                           |                                              256 KB                                              |                                         256 KB                                         |
|     ROM     |                                                 384 KB + 4 M                                                 |                                              16 KB + 2 M                                              |                                           512 KB                                           |                                               1 M                                               |                                           1 M                                           |
|  MCU Price  |                                                     \$0.5                                                     |                                                 \$0.8                                                 |                                            \$2                                            |                                               \$3                                               |                                          \$20                                          |
