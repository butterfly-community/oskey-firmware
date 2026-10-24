<!-- SPDX-License-Identifier: MPL-2.0 -->

[English](compile.md) · [快速使用](README_zh.md) · [产品展示](../showcase/README.md)

# 编译 OSKey

Dockerfile 用于 CI 编译环境。本地编译使用已有的 Zephyr 工作区，禁止在本地重建 Docker 镜像。本项目不建议使用 Windows 原生编译，Windows 用户请使用 WSL。

## 环境配置

务必仔细阅读

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## 加载开发环境

构建、烧录或运行任何 `west` 命令前，必须在同一个 shell 中加载全部三个环境。以下路径对应本地已有工作区，请按实际安装位置调整：

```sh
source ~/Develop/platform/zephyr-project/.venv/bin/activate
source ~/Develop/platform/zephyr-project/zephyr/zephyr-env.sh
source ~/.esp.sh

command -v west
test -n "$ZEPHYR_BASE"
rustc +esp --version
```

虚拟环境提供 `west` 和 Python 依赖，`zephyr-env.sh` 暴露 Zephyr 工作区，`.esp.sh` 加载 Xtensa 目标所需的 esp-rs 工具链。每次打开新的 shell 都需重新加载。

## 尝试编译示例

`esp32s3_devkitc/esp32s3/procpu` 是我们推荐的开发板。OSKey 要求原生支持指针宽度 atomic。

如果您已完成上述链接中的步骤，请尝试使用此命令编译示例。

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu \
  -d build/hello-world "$ZEPHYR_BASE/samples/hello_world"
```

## Rust 配置

**该项目还需要额外的 Rust 配置。请查看此处。**

[https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)

[https://docs.zephyrproject.org/latest/develop/languages/rust/index.html](https://docs.zephyrproject.org/latest/develop/languages/rust/index.html)

使用工程的 [`west.yml`](../../west.yml)，按[工作区配置说明](../../patch/README.md#reproduce-the-workspace)
更新全部依赖。Zephyr main 和 Rust 模块均固定到验证过的提交；从 OSKey 源码目录统一应用补丁：

```bash
python3 patch/apply.py /path/to/zephyr-project
```

另外可以参考 [Docker](../../Dockerfile)

**Xtensa 架构的 ESP32、ESP32-S2 和 ESP32-S3 需要上文加载的乐鑫 Rust 工具链。**

配置乐鑫 Rust 工具链

[https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html](https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html)


## 编译 OSKey

1. Clone 代码到本地，务必添加 `--recursive` 标志

   ```bash
   git clone --recursive https://github.com/butterfly-community/oskey-firmware.git
   cd oskey-firmware
   ```

2. 在当前 shell 中加载上文的开发环境，并按 Rust 配置中的链接完成固定版本的工作区配置。

3. 尝试编译

   ```bash
   west build -p always -b esp32s3_devkitc/esp32s3/procpu \
     -d build/esp32s3 -- -DCONFIG_OSKEY_STORAGE=y
   ```

4. 写入芯片

   ```bash
   west flash -d build/esp32s3
   ```

该配置启用持久存储和 UART 钱包通信。立创板的屏幕与外设使用下方配置；其他开发板配置及 overlay 见 [`build.ts`](../../build.ts)。

## 立创 ESP32-S3 屏幕与外设

在 OSKey 源码目录中执行，沿用已加载的三个环境：

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

蓝牙、Wi-Fi、USB、FIDO2、NXP 安全芯片和 MCUboot 更新需分别选择。相关配置见 [NXP 说明](../security/nxp-a5000.md)、[签名固件更新](firmware-update.md)和 [Linux 模拟器开发](linux-development.md)。

## 日志与文档图片

日志和启动横幅默认关闭。需要串口观察设备行为时，在构建命令的 `--` 后添加 `-DCONFIG_OSKEY_LOG=y`，再烧录并连接串口监视器。

当前文档图片位于 [`docs/showcase/`](../showcase/README.md)。[截图工具说明](../../src/tests/showcase/README.md)介绍如何使用已有 Zephyr 环境重新生成和校验这些资源。
