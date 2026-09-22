<!-- SPDX-License-Identifier: MPL-2.0 -->

# Linux Native Development

Use Zephyr's `native_sim` target to develop OSKey on a Linux host. SDL2, `socat`, and `usbip`
are required for the corresponding features.

## Environment

```sh
export ZEPHYR_WORKSPACE=/path/to/zephyr-project

source "$ZEPHYR_WORKSPACE/.venv/bin/activate"
source "$ZEPHYR_WORKSPACE/zephyr/zephyr-env.sh"
source /path/to/esp-rs-env.sh

command -v west
test -n "$ZEPHYR_BASE"
rustc +esp --version
```

## Virtual UART

Keep the UART bridge running in a separate terminal:

```sh
socat -d -d \
  pty,raw,echo=0,link=/tmp/ttyOSKey \
  pty,raw,echo=0,link=/tmp/ttyOSKeyC
```

The simulator uses `/tmp/ttyOSKey`; host tools use `/tmp/ttyOSKeyC`.

## Zephyr Patches

Use OSKey's [`west.yml`](../../west.yml) to pin Zephyr, Rust, and the imported dependencies.
Follow the [workspace setup and update instructions](../../patch/README.md#reproduce-the-workspace),
then apply all retained patches from the OSKey source directory:

```sh
python3 patch/apply.py "$ZEPHYR_WORKSPACE"
```

The patch audit and removal conditions are recorded in [`patch/README.md`](../../patch/README.md).
CTAP `changePIN` is now provided by upstream Zephyr.

## Build

Display:

```sh
west build -p always \
  -b native_sim/native/64 \
  -- \
  -DCONFIG_OSKEY_DISPLAY=y
```

Display assets are committed and require no host image tools for normal builds. To regenerate
them after editing an SVG, install `gdk-pixbuf-thumbnailer` and ImageMagick, then run:

```sh
west build -t display-assets
```

The target exits successfully without changing the committed assets when either tool is not
available.

LVGL benchmark:

```sh
west build -p always \
  -b native_sim/native/64 \
  -- \
  -DCONFIG_OSKEY_DISPLAY=y \
  -DCONFIG_OSKEY_LVGL_BENCHMARK=y
```

The application selects Native Simulator's pseudo-host real-time clock as the LVGL tick source
for this benchmark, so no LVGL source patch is required.

Display and FIDO2 over USB/IP:

```sh
west build -p always \
  -b native_sim/native/64 \
  -S usbip-native-sim \
  -- \
  -DCONFIG_OSKEY_DISPLAY=y \
  -DCONFIG_OSKEY_USB=y \
  -DCONFIG_OSKEY_FIDO2=y \
  -DEXTRA_DTC_OVERLAY_FILE=boards/overlay/fido2.overlay
```

## Run

Run from a terminal inside the RDP session:

```sh
echo "$DISPLAY"
SDL_RENDER_DRIVER=software west build -t run
```

If no window appears, check that `$DISPLAY` is not empty. Start `socat` first if the simulator
cannot open `/tmp/ttyOSKey`.

## USB/IP

Before running the USB/IP build, create the TAP interface in another terminal:

```sh
cd "$ZEPHYR_BASE/../tools/net-tools"
./net-setup.sh
```

After starting the simulator, attach its USB device:

```sh
sudo modprobe vhci_hcd
usbip list -r 192.0.2.1
sudo usbip attach -r 192.0.2.1 -b 1-1
```

Detach it with:

```sh
usbip port
sudo usbip detach -p 0
```

If the simulator cannot create `zeth`, start `net-setup.sh` before running it.
