## OHW 是什么？

  ohw 定义是一个完全开源的非商业产品的硬件钱包，我们和其他产品不同的是我们决定不生产和制造自有品牌的专有的硬件。
  目标是帮助每一个人创造属于自己的硬件钱包。

## 创造属于自己的硬件钱包

  你的硬件钱包其实不属于你自己，就算开源做的很不错的产品，也存在非常多的隐形限制阻止你拥有自定义的固件和硬件，绝大部分用户也没有这个能力。

  我们的固件完全开放，硬件本身可以使用目前市面上大多数芯片厂商的芯片和芯片的开发板。不存在任何的隐形限制。支持 10+ 芯片厂商和 200+ 款 Soc。

  我们使用经过安全认证的加密库和芯片内置安全策略，保护使用者的资产安全。

  我们使用经过成本优化的开发策略，支持的芯片最低价格为 0.3 美元。

## 如何使用固件

  我们使用 Zephyr RTOS，Zephyr 是一个开发者友好的实时操作系统。

### 预编译固件

  我们会为我们拥有的开发板预编译固件，请查看右侧 Release 下载或者下文查看我们拥有哪些开发板，直接使用预编译固件请查看...。

### 自编译固件

  如果开发板不在预编译固件中，请查看以下链接设置开发环境，为开发板编译固件。

  [https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

  也可以选择尝试使用 Docker 简化这个过程。

  [https://github.com/zephyrproject-rtos/docker-image](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## 开发板

  我们拥有的开发板拥有 Tire 1 级别的支持，开发者会在这些开发板上开发测试。

  除了我们拥有的开发板，也支持其他 300+ 款开发板，请查看支持列表 [Supported Boards and Shields](https://docs.zephyrproject.org/latest/boards/index.html)。

| 名称 |                           ESP32-C3-DevKitM-1                           |             Raspberry Pi Pico             |                   nRF52840-MDK                   |               NXP FRDM-K64F               |
| :--: | :--------------------------------------------------------------------: | :---------------------------------------: | :-----------------------------------------------: | :---------------------------------------: |
| 图片 | ![esp32-c3-devkitm](doc/image/board/esp32-c3-devkitm-1-v1-isometric.png) | ![rpi-pico](doc/image/board/pico-board.png) | ![nrf52840-mdk](doc/image/board/mdk52840-cover.png) | ![frdm_k64f](doc/image/board/frdm_k64f.jpg) |
| 厂商 |                               Espressif                               |           Nordic Semiconductor           |                   Raspberry Pi                   |                    NXP                    |
| 芯片 |                              ESP32-C3FH4                              |                  RP2040                  |                     nRF52840                     |              MK64FN1M0VLL12              |
| 架构 |                                 RISC-V                                 |               Arm Cortex-M0               |                  ARM Cortex-M4F                  |              ARM Cortex-M4              |
| RAM |                                 400 KB                                 |                  264 KB                  |                      256 KB                      |                  256 KB                  |
| ROM |                              384 KB + 4M                              |                16 KB + 2 M                |                        1 M                        |                    1M                    |
| 价格 |                                 \$1.5                                 |                    \$2                    |                       \$25                       |                   \$50                   |
