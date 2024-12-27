## OHW 是什么？

  OHW 定义是一个完全开源的非商业产品的硬件钱包，我们会创建一个完全开源的非商业化的硬件钱包。

  我们的使命是帮助您创造自己的硬件钱包，无论是软件还是硬件。

  您可以随意选择自己喜欢的品牌的开发板或者类似 Arduino 和 Raspberry Pi 等开源硬件或者芯片制造商提供的开发板或者各种第三方厂商推出的开发板还可以自己做板子。

  OHW 支持多种硬件架构，针对资源受限设备进行了优化，并在最开始设计时就考虑安全性。支持的最便宜的 MCU 价格仅为 0.3 美元，其他还有可选的支持蓝牙和 WiFi 还有屏幕。

## OHW 可以做什么?

我们构建了区块链和芯片的连接器，这不仅仅是一个硬件钱包。

#### 演示视频:

[![OHW](https://res.cloudinary.com/marcomontalbano/image/upload/v1733827828/video_to_markdown/images/youtube--JkhVWNCGZvg-c05b58ac6eb4c4700831b2b3070cd403.jpg)](https://www.youtube.com/watch?v=JkhVWNCGZvg "OHW")

## 如何使用固件

### 预编译固件

  我们会为我们拥有的开发板预编译固件，请查看右侧 Release 下载或者下文查看我们拥有哪些开发板。

### 自编译固件

  如果开发板不在预编译固件中，请查看以下链接设置开发环境，为开发板编译固件。

  [https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

  也可以选择尝试使用 Docker 简化这个过程。

  [https://github.com/zephyrproject-rtos/docker-image](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

  额外需要注意的是我们使用 Rust 这个更安全更现代的语言完成钱包功能，所以需要额外设置 Rust 编译环境。请参考以下链接。

  [https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)

  [https://github.com/zephyrproject-rtos/zephyr-lang-rust](https://github.com/zephyrproject-rtos/zephyr-lang-rust)

  [https://github.com/zephyrproject-rtos/zephyr-lang-rust/discussions/11#discussioncomment-10905800](https://github.com/zephyrproject-rtos/zephyr-lang-rust/discussions/11#discussioncomment-10905800)

## 开发板

  我们拥有的开发板拥有 Tire 1 级别的支持，开发者会在这些开发板上开发测试。

  除了我们拥有的开发板，也支持其他 300+ 款开发板，请查看支持列表 [Supported Boards and Shields](https://docs.zephyrproject.org/latest/boards/index.html)。

  由于开发板型号太多，这里只写了芯片的价格。请自行选择喜欢的开发板。

|   名称   |                           ESP32-C3-DevKitM-1                           |             Raspberry Pi Pico             |                 Nucleo F401RE                 |                   nRF52840-MDK                   |               NXP FRDM-K64F               |
| :------: | :--------------------------------------------------------------------: | :---------------------------------------: | :-------------------------------------------: | :-----------------------------------------------: | :---------------------------------------: |
|   图片   | ![esp32-c3-devkitm](doc/image/board/esp32-c3-devkitm-1-v1-isometric.png) | ![rpi-pico](doc/image/board/pico-board.png) | ![stm32f401](doc/image/board/nucleo_f401re.jpg) | ![nrf52840-mdk](doc/image/board/mdk52840-cover.png) | ![frdm_k64f](doc/image/board/frdm_k64f.jpg) |
|   厂商   |                               Espressif                               |               Raspberry Pi               |              STMicroelectronics              |               Nordic Semiconductor               |                    NXP                    |
|   芯片   |                              ESP32-C3FH4                              |                  RP2040                  |                 STM32F401RET6                 |                     nRF52840                     |              MK64FN1M0VLL12              |
|   架构   |                                 RISC-V                                 |               Arm Cortex-M0               |                 ARM Cortex-M4                 |                   ARM Cortex-M4                   |               ARM Cortex-M4               |
|   RAM   |                                 400 KB                                 |                  264 KB                  |                     96 KB                     |                      256 KB                      |                  256 KB                  |
|   ROM   |                              384 KB + 4 M                              |                16 KB + 2 M                |                    512 KB                    |                        1 M                        |                    1 M                    |
| 芯片价格 |                                 \$0.5                                 |                   \$0.8                   |                      \$2                      |                        \$3                        |                   \$20                   |
