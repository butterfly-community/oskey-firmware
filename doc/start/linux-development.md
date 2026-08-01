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

## Build

Display:

```sh
west build -p always \
  -b native_sim/native/64 \
  -- \
  -DCONFIG_OSKEY_DISPLAY=y
```

LVGL benchmark:

Apply the Native Simulator timing fix once from the OSKey source directory:

```sh
git -C "$ZEPHYR_WORKSPACE/modules/lib/gui/lvgl" apply \
  "$(pwd)/patch/lvgl-native-sim-benchmark.patch"
```

```sh
west build -p always \
  -b native_sim/native/64 \
  -- \
  -DCONFIG_OSKEY_DISPLAY=y \
  -DCONFIG_OSKEY_LVGL_BENCHMARK=y
```

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
