<!-- SPDX-License-Identifier: MPL-2.0 -->

Dockerfile 用于 CI 编译环境。本地编译使用已有的 Zephyr 工作区，禁止在本地重建 Docker 镜像。本项目不建议使用 Windows 原生编译，Windows 用户请使用 WSL。

## 环境配置

务必仔细阅读

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## 尝试编译示例

`esp32s3_devkitc/esp32s3/procpu` 是我们推荐的开发板。OSKey 要求原生支持指针宽度 atomic。

如果您已完成上述链接中的步骤，请尝试使用此命令编译示例。

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu samples/hello_world
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

**如果芯片为 Xtensa 架构的 ESP32、ESP32-S2 或 ESP32-S3，其他芯片可忽略此项**

配置乐鑫 Rust 工具链

[https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html](https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html)


## 编译 OSKey

1. Clone 代码到本地，务必添加 `--recursive` 标志

   ```bash
   git clone --recursive https://github.com/butterfly-community/oskey-firmware.git
   ```

2. 设置环境变量

   ```bash
   source ~/zephyrproject/zephyr/zephyr-env.sh
   ```

3. 尝试编译

   ```bash
   west build -p always -b esp32s3_devkitc/esp32s3/procpu
   ```

4. 写入芯片

   ```bash
   west flash
   ```
