<!-- SPDX-License-Identifier: MPL-2.0 -->

# Lichuang ESP32-S3 Audio

## Build configurations

USB and microphone support are separate features. A regular USB build does not include the
ES7210 driver, the UAC2 function, or the microphone capture thread:

```conf
CONFIG_OSKEY_USB=y
CONFIG_OSKEY_MICROPHONE=n
```

Enable the board microphone explicitly to add a USB Audio Class 2 recording interface alongside
the existing USB functions:

```conf
CONFIG_OSKEY_USB=y
CONFIG_OSKEY_MICROPHONE=y
```

`CONFIG_OSKEY_MICROPHONE` selects the ES7210 codec driver, Zephyr UAC2 support, and the base audio
service. The microphone option depends on USB because the application currently exposes captured
audio only through UAC2. The status-bar icon remains present in builds without microphone support;
it is shown as disabled and reports that microphone support is not enabled when pressed.

## Hardware and format

The Lichuang ESP32-S3 board connects the ES7210 at I2C address `0x41`. Its audio signals use GPIO
38 for MCLK, GPIO 14 for BCLK, GPIO 13 for WS, and GPIO 12 for data input. The application captures
MIC1 and MIC2 as the left and right channels of standard stereo I2S.

The USB recording format is stereo 16-bit PCM. The host can select 16, 32, or 48 kHz. Each rate
uses a 256-times MCLK supported by both the ESP32-S3 I2S controller and the ES7210. The current
Espressif ES7210 coefficient table includes the 8.192 MHz MCLK combination required for 32 kHz.

MIC3 is wired to the ES8311 playback output as a hardware echo-reference input. It is intentionally
not exposed: capturing it requires a TDM and multichannel pipeline, while the current interface is
a simple two-channel microphone. No echo cancellation, noise suppression, automatic gain control,
or local microphone playback is applied.

## Privacy switch and runtime behavior

Microphone capture is off after every boot and must be enabled manually with the microphone icon
in the top status bar. The setting is not persisted.

- When the switch is off, the USB recording interface remains available but sends silence. Opening
  the interface on the host cannot enable physical microphone capture.
- When the switch is on, ES7210 and I2S capture starts only while a host application has the UAC2
  recording stream open.
- Changing the sample rate stops and reconfigures ES7210 and I2S. The USB interface remains online
  and sends silence during the transition.
- Closing the host stream or turning off the privacy switch stops capture.
- Playing a local notification sound pauses capture. UAC2 sends silence and capture resumes after
  playback finishes.

## Data path and clock domains

```text
MIC1/MIC2 -> ES7210 -> ESP32-S3 I2S RX -> PCM ring buffer -> Zephyr UAC2 -> USB host
```

ES7210/I2S and USB SOF use separate clock domains and therefore have a small frequency mismatch.
A short PCM ring buffer decouples them. For each 1 ms USB frame, the implementation may consume one
stereo sample more or less than the nominal count to keep the buffer near its target level. It does
not resample or otherwise alter the PCM samples.

Board wiring and the UAC2 topology are defined in `boards/esp32s3_lichuang.overlay`. The codec
driver is in `drivers/audio/`; application capture, USB adaptation, and the status-bar control are
in `src/audio/` and `src/display/`.

## Upstream references

- [Espressif `esp_codec_dev` ES7210 driver](https://github.com/espressif/esp-adf/blob/release/v2.x/components/esp_codec_dev/device/es7210/es7210.c)
- [Lichuang ESP32-S3 ES7210 board guide](https://openkits-wiki.easyeda.com/zh-hans/szpi-esp32s3/beginner/audio-input-es7210.html)
