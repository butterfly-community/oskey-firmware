#### Getting Started

[https://docs.zephyrproject.org/latest/develop/getting_started/index.html](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

`esp32c3_devkitm` 是我们推荐的开发板，如果您拥有的是其他的开发板，请查看支持列表 [Supported Boards](https://docs.zephyrproject.org/latest/boards/index.html).

如果您已完成上述链接中的步骤，请尝试使用此命令编译示例。

```bash
west build -p always -b esp32c3_devkitm samples/hello_world
```

**该项目还需要额外的 Rust 配置。请查看此处。**

[https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install)

[https://docs.zephyrproject.org/latest/develop/languages/rust/index.html](https://docs.zephyrproject.org/latest/develop/languages/rust/index.html)

**另外可以参考** **[Dockerfile](../../Dockerfile)**

**如果芯片为 Espressif（乐鑫） ESP32/ESPS2/ESPS3 系列**

配置乐鑫 Rust 工具链

[https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html](https://docs.espressif.com/projects/rust/book/installation/riscv-and-xtensa.html)

配置 Rust 补丁

```bash
cd <YOUR_ZEPHYR_PATH>/modules/lang/rust

wget https://raw.githubusercontent.com/butterfly-community/oskey-firmware/refs/heads/master/rust.patch

git apply rust.patch
```

#### Compile OSKey

1. Clone 代码到本地，务必添加 `--recursive` 标志

   ```bash
   git clone --recursive https://github.com/butterfly-community/oskey-firmware.git
   ```

2. 设置环境变量

   > Please refer to [here](https://docs.zephyrproject.org/latest/develop/env_vars.html#zephyr-environment-scripts) for the Windows environment.

   ```bash
   # Mac or Linux environment
   source ~/zephyrproject/zephyr/zephyr-env.sh
   ```

3. 尝试编译

   ```bash
   west build -p always -b esp32c3_devkitm
   ```

4. 写入芯片

   ```bash
   west flash
   ```
