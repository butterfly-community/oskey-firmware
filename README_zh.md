<!-- SPDX-License-Identifier: MPL-2.0 -->

## OSKey 是什么？

OSKey (Open Source Key) 是一个完全开源的非商业硬件钱包项目。我们的首要使命是帮助您创建并使用自己的无需信任的硬件钱包，并最终成为您在数字世界管理身份的关键设施。

与使用开源来推动其商业产品销售的商业硬件产品不同，我们的开源重点是构建开源基础设施，打造一个去信任的开源软硬件系统。

我们不限制特定的芯片制造商或型号。用户可以自由选择来自 10 多家制造商的 200 多种芯片，并可以使用芯片制造商或第三方制造的 3000 多种开发板，例如 Arduino 或 Raspberry Pi 等产品。

用户还可以根据我们即将发布的参考设计创建自己的开发板。

OSKey 支持多种硬件架构，针对资源受限设备进行了优化，并从设计之初就考虑安全性。支持的最便宜的 MCU 价格仅为 0.3 美元，并可按需选配蓝牙、Wi-Fi 和屏幕。

OSKey 已经准备好一组可以自由组合的功能模块。它们像硬件世界的积木，把钱包、安全、连接与交互的能力交到你手中。你可以从一块小巧的开发板出发，也可以围绕自己的生活习惯，组合出一台带有个人风格的设备：

- **钱包与签名：** 助记词生成与导入、HD 钱包派生、消息与交易签名。
- **PIN 与安全芯片：** 钱包锁定与解锁、seed 存储、NXP A5000 管理。
- **FIDO2 身份认证：** USB 身份认证与设备端确认。
- **屏幕与触摸：** 设备界面、触摸输入与签名确认。
- **连接：** 蓝牙、Wi-Fi、USB 与 MQTT。
- **摄像头与二维码：** 图像采集、二维码扫描与气隙签名。
- **音频：** 扬声器输出与音量控制。
- **运动感知：** 六轴加速度与陀螺仪数据采集、姿态融合。
- **存储：** 设置持久化与 SD 卡文件浏览。
- **固件更新：** 通过蓝牙或 UART 更新签名固件。

### 硬件主权

随着 AI 的发展，我们现在拥有了硬件主权，也拥有了把想象变成实物的自由。用自己的语言描述需求，借助 AI 组合模块、绘制 PCB、设计外壳，让脑海中的轮廓逐渐成为手中可握的实物。一个念头、一张草图，都可以成为创造的起点。

它可以是一枚随身携带的小巧钥匙，也可以是一台带有屏幕、摄像头和触摸交互的桌面伙伴。电路板的形状、接口的位置、外壳的线条与触感，都可以随你的喜好而定。你选择它如何工作，也选择它如何融入生活。

一件属于你的硬件，既承载你的密钥，也承载你的选择。OSKey 提供可组合的能力，AI 帮助想法落地。从模块的取舍，到 PCB 的布局，再到外壳握在掌心的感觉，设备最终的模样，由你决定。

## OSKey 可以做什么？

我们构建了数字世界和真实世界的桥梁，这不仅仅是一个硬件钱包。

### 快速指南

**[快速使用指南](https://github.com/butterfly-community/oskey-firmware/tree/master/docs/start)**

### 功能

#### ✅ 芯片内助记词生成和导入

[BIP39](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki) 通过所有 [单元测试](https://github.com/butterfly-community/oskey-lib-wallets/blob/main/src/mnemonic.rs)。

#### ✅ HD（Hierarchical Deterministic）分层确定性钱包

[BIP32](https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki) 通过所有 [单元测试](https://github.com/butterfly-community/oskey-lib-wallets/blob/main/src/wallets.rs)。

#### ✅ 以太坊消息与交易签名

支持以太坊个人消息（EIP-191）和交易（EIP-2930）签名，在设备上查看签名内容并确认操作。

#### ✅ 气隙签名

通过二维码交换签名请求与结果。使用摄像头扫描请求，在设备上查看、确认并完成签名，再通过屏幕上的二维码传回签名结果。

#### ✅ PIN 钱包管理

通过 PIN 解锁和锁定钱包，管理 seed 存储，并在设备界面中完成钱包初始化与清除。

#### ✅ 可选 NXP 安全芯片

使用 NXP A5000 安全芯片存储 seed，通过 PIN 验证控制访问。芯片执行十次尝试限制，锁定后通过清除钱包并重新初始化恢复使用。专属管理页面提供芯片状态查看、初始化与钱包清除操作。关闭该模块时使用软件钱包存储链路。

#### ✅ FIDO2 身份认证

通过 USB 将 OSKey 用作 FIDO2 认证器，支持 OpenSSH 等 FIDO2 应用，并在设备上确认认证操作。

#### ✅ 连接与硬件交互

支持蓝牙连接和 Wi-Fi 配网，通过触摸屏、摄像头、音频、六轴 IMU 和 SD 卡文件浏览器提供设备交互。

#### ✅ 签名固件更新

通过蓝牙或 UART 更新固件，使用 MCUboot 验证固件签名并提供版本防回退保护。

#### ✅ 模块化功能组合

钱包、安全芯片、屏幕、连接与外设模块均可在编译时按需选择，根据硬件配置和使用场景组合固件功能。

### 功能展示

#### 初始化选择

<img src="docs/image/demo/demo-1a.jpg" alt="demo" width="150" style="max-width:100%; height:auto;"> <img src="docs/image/demo/demo-1b.jpg" alt="demo" width="150" style="max-width:100%; height:auto;">

#### 生成助记词

<img src="docs/image/demo/demo-2a.jpg" alt="demo" width="150" style="max-width:100%; height:auto;"> <img src="docs/image/demo/demo-2b.jpg" alt="demo" width="150" style="max-width:100%; height:auto;"> <img src="docs/image/demo/demo-2c.jpg" alt="demo" width="150" style="max-width:100%; height:auto;">

#### 自定义生成助记词

<img src="docs/image/demo/demo-2a.jpg" alt="demo" width="150" style="max-width:100%; height:auto;"> <img src="docs/image/demo/demo-3a.jpg" alt="demo" width="150" style="max-width:100%; height:auto;"> <img src="docs/image/demo/demo-3b.jpg" alt="demo" width="150" style="max-width:100%; height:auto;">

#### 导入助记词

<img src="docs/image/demo/demo-4a.jpg" alt="demo" width="150" style="max-width:100%; height:auto;">

## 如何使用固件

### 预编译固件

我们会为我们拥有的开发板预编译固件，请查看右侧 [Release](https://github.com/butterfly-community/oskey-firmware/releases) 下载，或在下文查看我们拥有的开发板。

### 自编译固件

如果开发板不在预编译固件中，请查看以下链接设置开发环境，为开发板编译固件。

[点击此处](docs/start/compile_zh.md)

## 开发板

除了我们拥有的开发板，也直接支持其他 300+ 款开发板，请查看支持列表 [Supported Boards](https://docs.zephyrproject.org/latest/boards/index.html)。

由于开发板型号太多，这里只写了芯片的价格。请自行选择喜欢的开发板。

### 基础体验

我们特意选择了来自 2 家不同芯片制造商、采用同一种架构的 2 款开发板作为官方支持，以展示我们不受供应商锁定的能力。开发者会在这些开发板上开发测试。

<br />

| 名称 | [Nucleo F401RE](https://docs.zephyrproject.org/latest/boards/st/nucleo_f401re/doc/index.html) | [nRF52840-MDK](https://docs.zephyrproject.org/latest/boards/makerdiary/nrf52840_mdk/doc/index.html) |
| :--: | :-------------------------------------------------------------------------------------------: | :-------------------------------------------------------------------------------------------------: |
| 图片 |                        ![stm32f401](docs/image/board/nucleo_f401re.jpg)                        |                         ![nrf52840-mdk](docs/image/board/mdk52840-cover.png)                         |
| 厂商 |                                      STMicroelectronics                                       |                                        Nordic Semiconductor                                         |
| 芯片 |                                         STM32F401RET6                                         |                                              nRF52840                                               |
| 架构 |                                         ARM Cortex-M4                                         |                                            ARM Cortex-M4                                            |

### 完整体验

这些型号具有屏幕和触摸支持，可以体验硬件钱包的完整功能。

|   名称   |                                 [Lichuang ESP32-S3](https://item.szlcsc.com/43285221.html)                                 |    [STM32H747I Discovery](https://docs.zephyrproject.org/latest/boards/st/stm32h747i_disco/doc/index.html)    |
| :------: | :------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------------: |
|   图片   | <img src="docs/image/board/lichuang_esp32_s3.jpg" alt="esp32-s3" width="200" style="max-width:100%; height:auto;"> | <img src="docs/image/board/stm32h747i_disco.jpg" alt="stm32" width="200" style="max-width:100%; height:auto;"> |
| 屏幕尺寸 |                                                           2-inch                                                           |                                                    4-inch                                                     |

默认情况下开发板的芯片未经过安全锁定，不具备任何安全功能。如何锁定芯片请查询对应芯片的文档。

## Powered by

| <a href="https://www.gccofficial.org/" target="_blank"><img src="docs/image/GCC_logo.png" alt="gcc" width="200" style="max-width:100%; height:auto;"></a> | <a href="https://openbuild.xyz/" target="_blank"><img src="docs/image/OpenBuild_logo.png" alt="OpenBuild" width="200" style="max-width:100%; height:auto;"></a> |
| -------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |

## 许可证

OSKey 原创文件使用 Mozilla Public License 2.0。来自 Zephyr、Espressif
及其他第三方项目的文件继续保留各自的许可证声明。详情请参阅
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
