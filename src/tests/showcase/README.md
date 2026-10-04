<!-- SPDX-License-Identifier: MPL-2.0 -->

# OSKey 产品展示截图

本应用复用 `src/display/` 中的 LVGL 页面，以固定展示数据渲染完整的产品截图。钱包状态、签名请求、Wi-Fi 网络、NXP 状态、姿态数据和文件目录由 `fixtures.c` 提供。气隙流程、MQTT、麦克风与更新流程使用同一套 UI 控件组成展示页。FIDO 请求使用 `www.google.com` 和 `demo@example.com`。

展示应用单独构建，输出目录为 `build/showcase/`；产品文档和 PNG 截图位于 `docs/showcase/`。原始帧写入构建目录下的临时目录，转换后自动清理。

## 生成截图

在项目根目录执行：

```sh
source ~/Develop/platform/zephyr-project/.venv/bin/activate
source ~/Develop/platform/zephyr-project/zephyr/zephyr-env.sh
source ~/.esp.sh

command -v west
test -n "$ZEPHYR_BASE"
rustc +esp --version

python3 src/tests/showcase/capture.py
```

程序使用 SDL dummy 驱动和 LVGL snapshot 离屏渲染，生成 Markdown 首页、PNG 截图、总览拼图与清单。Python 环境需要 Pillow。

复用已构建的展示程序：

```sh
python3 src/tests/showcase/capture.py --skip-build
```

## 校验

```sh
python3 src/tests/showcase/capture.py --verify
```

校验覆盖所有 `UI_PAGE_*` 页面，逐项检查截图的尺寸、有效画面、可见文字、SHA-256、首页引用和本地链接，同时核对 Google FIDO 示例、生成输入文件的一致性和五张二维码的实际解码内容。二维码校验复用项目的 quirc 解码库。

`catalog.json` 记录每个场景的功能分组、截图名称、页面来源和预期可见文字。增加界面页时，校验会指出需要补充的展示场景。
