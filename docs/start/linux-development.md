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

The current patches are tested with Zephyr `5058917ea61b9d075f24c0dfeb89fdb95a405d52`.
Apply them in this order from the OSKey source directory:

```sh
git -C "$ZEPHYR_BASE" apply "$PWD/patch/tf-psa-threading.patch"
git -C "$ZEPHYR_BASE" apply "$PWD/patch/esp32-virtual-efuse.patch"
git -C "$ZEPHYR_BASE" apply "$PWD/patch/fido/change-pin.patch"
git -C "$ZEPHYR_BASE" apply "$PWD/patch/fido/oskey.patch"
git -C "$ZEPHYR_BASE" apply "$PWD/patch/fido/usb-busy.patch"
git -C "$ZEPHYR_BASE" apply "$PWD/patch/fido/keepalive.patch"
git -C "$ZEPHYR_BASE" apply "$PWD/patch/usb-dwc2-zlp-cache.patch"
```

`change-pin.patch` mirrors upstream Zephyr PR #115238 and can be removed once that change is
present in the Zephyr tree. See [`patch/README.md`](../../patch/README.md) for the audit status and
removal condition of every patch.

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
