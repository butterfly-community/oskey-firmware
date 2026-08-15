<!-- SPDX-License-Identifier: MPL-2.0 -->

# Firmware Update

OSKey uses MCUboot test swaps with signed P-256 images. A new image is confirmed only after the
application finishes startup successfully, and MCUboot rejects older versions. The application
supports either the Zephyr or Espressif MCUboot port.

The application uses the board's default A/B flash layout. MCUboot itself has no secondary slot.

## Zephyr port

Build the bootloader and application together with sysbuild:

```sh
west build --sysbuild -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  -S espressif-flash-16M \
  -S espressif-psram-8M \
  -- \
  -DCONFIG_OSKEY_MCUBOOT_ZEPHYR=y \
  -DCONFIG_OSKEY_BLUETOOTH=y \
  -DEXTRA_CONF_FILE=boards/esp32s3_lichuang.conf \
  -DEXTRA_DTC_OVERLAY_FILE="boards/esp32s3_lichuang.overlay;boards/overlay/mcumgr_uart0.overlay"
```

## Espressif port

The Espressif port uses persistent virtual eFuses by default, allowing Secure Boot and flash
encryption flows to run on hardware without changing real eFuses. The virtual state occupies the
board's `sys_partition` at `0x10000`.

### Build

Prepare ESP-IDF 6.0 and the common paths:

```sh
app_dir=$PWD
mcuboot_dir=$(west list mcuboot -f '{abspath}')
esp_port="$mcuboot_dir/boot/espressif"
upstream_profile="$esp_port/port/esp32s3/bootloader.conf"
overlays="boards/esp32s3_lichuang.overlay;boards/overlay/mcuboot_esp.overlay;boards/overlay/mcumgr_uart0.overlay"
board_conf="boards/esp32s3_lichuang.conf"
port=/dev/ttyACM0

source /path/to/esp-idf/export.sh
python -m pip install -r "$mcuboot_dir/scripts/requirements.txt"
```

Select one profile in the same shell. Development uses repository test keys and virtual eFuses:

```sh
output=temp/development
profile="$app_dir/mcuboot/esp.conf"
mcuboot_key="$app_dir/sign/root-ec-p256.pem"
secure_boot_key="$app_dir/sign/root-rsa-3072.pem"
virtual_efuse=y
```

Production permanently provisions real eFuses. Never select this profile in development or
automated tests. Store both production keys outside the source tree:

```sh
output=temp/production
profile="$app_dir/mcuboot/esp-production.conf"
mcuboot_key=/secure/path/mcuboot-p256.pem
secure_boot_key=/secure/path/secure-boot-rsa3072.pem
virtual_efuse=n
```

Build and sign the standalone bootloader:

```sh
cmake -S "$esp_port" -B "$output/mcuboot" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$esp_port/tools/toolchain-esp32s3.cmake" \
  -DMCUBOOT_TARGET=esp32s3 \
  -DESP_HAL_PATH="$IDF_PATH" \
  -DMCUBOOT_CONFIG_FILE="$upstream_profile;$profile" \
  -DCONFIG_ESP_SIGN_KEY_FILE="$mcuboot_key"
cmake --build "$output/mcuboot"

espsecure sign-data --version 2 --keyfile "$secure_boot_key" \
  --output "$output/mcuboot/mcuboot_esp32s3.signed.bin" \
  "$output/mcuboot/mcuboot_esp32s3.bin"
```

Build the application without sysbuild:

```sh
west build -p always \
  -b esp32s3_devkitc/esp32s3/procpu \
  -S espressif-flash-16M \
  -S espressif-psram-8M \
  --build-dir "$output/app" \
  -- \
  -DCONFIG_OSKEY_MCUBOOT_ESP=y \
  -DCONFIG_OSKEY_MCUBOOT_ESP_VIRTUAL_EFUSE="$virtual_efuse" \
  -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE="$mcuboot_key" \
  -DCONFIG_OSKEY_BLUETOOTH=y \
  -DEXTRA_CONF_FILE="$board_conf" \
  -DEXTRA_DTC_OVERLAY_FILE="$overlays"
```

Verify both outputs:

```sh
espsecure verify-signature --version 2 --keyfile "$secure_boot_key" \
  "$output/mcuboot/mcuboot_esp32s3.signed.bin"
python "$mcuboot_dir/scripts/imgtool.py" verify --key "$mcuboot_key" \
  "$output/app/zephyr/zephyr.signed.bin"
```

Only these variables normally need to be selected:

| Variable | Purpose |
| --- | --- |
| `output` | Keeps development and production artifacts separate. |
| `profile` | Selects virtual development eFuses or irreversible production eFuses. |
| `mcuboot_key` | Signs application images and is compiled into MCUboot for verification. |
| `secure_boot_key` | RSA-3072 key used by ESP Secure Boot V2 to sign MCUboot itself. |
| `virtual_efuse` | Stores simulated eFuse state in flash when set to `y`. |
| `port` | Serial port used for writing and inspecting the device. |

The remaining CMake options select the ESP32-S3 port, ESP-IDF HAL, OSKey profile, and P-256
application key. The overlays declare the 32-byte encrypted flash write block and select UART0 for
application-requested MCUboot updates. `CONFIG_OSKEY_BLUETOOTH=y` enables authenticated BLE SMP
updates and can be removed when Bluetooth updates are not required.

### Development write

Use this procedure only after selecting the development profile. The two images use the board's
default layout:

| Address | Image |
| --- | --- |
| `0x0` | ESP Secure Boot-signed MCUboot bootloader |
| `0x20000` | MCUboot P-256-signed primary application |

```sh
esptool --chip esp32s3 --port "$port" erase-flash
esptool --chip esp32s3 --port "$port" write-flash \
  0x0 "$output/mcuboot/mcuboot_esp32s3.signed.bin" \
  0x20000 "$output/app/zephyr/zephyr.signed.bin"
```

The first boot updates only the virtual eFuse state stored in flash. Changing the test Secure Boot
key requires erasing the virtual state before booting the new image.

### Production write

Production provisioning permanently enables Secure Boot and flash encryption in real eFuses. It
is irreversible. Use a new, unprovisioned device and stable power. The keys under `sign/` are test
keys and must not be used.

Record and review the initial security state:

```sh
espefuse --chip esp32s3 --port "$port" summary --format json \
  --file "$output/efuse-before.json"
esptool --chip esp32s3 --port "$port" get-security-info
```

Stop if Secure Boot or flash encryption is already enabled, or if security key eFuses contain an
unexpected digest. Do not attempt to provision such a device again.

Erase, stage, and verify both plaintext images without starting them:

```sh
esptool --chip esp32s3 --port "$port" erase-flash
esptool --chip esp32s3 --port "$port" --after no-reset write-flash \
  0x0 "$output/mcuboot/mcuboot_esp32s3.signed.bin" \
  0x20000 "$output/app/zephyr/zephyr.signed.bin"
esptool --chip esp32s3 --port "$port" --after no-reset verify-flash \
  0x0 "$output/mcuboot/mcuboot_esp32s3.signed.bin" \
  0x20000 "$output/app/zephyr/zephyr.signed.bin"
```

The following command crosses the irreversible boundary. It starts the production bootloader,
which provisions real eFuses and encrypts flash. Do not interrupt power until the application has
finished starting:

```sh
esptool --chip esp32s3 --port "$port" run
```

After provisioning, enter ROM download mode and verify the resulting security state with read-only
commands:

```sh
espefuse --chip esp32s3 --port "$port" summary
esptool --chip esp32s3 --port "$port" get-security-info
```

Do not use `west flash`, `erase-flash`, or plaintext `write-flash` on a provisioned device. Future
application updates must use a newer `zephyr.signed.bin` through MCUboot. MCUboot itself cannot be
updated over the air after production provisioning because ESP32-S3 has no recovery bootloader and
the current layout has no secondary bootloader slot.

Keep the P-256 application key for future application releases. The RSA-3072 Secure Boot key is
needed to reproduce the factory bootloader but does not provide a safe field-update path for it.

UART updates are enabled for one boot by the OSKey `FirmwareUpdateRequest`; the normal wallet UART
is not started in that mode. Bluetooth updates use the authenticated SMP service while the normal
application is running.

Use `mcumgr` over UART. For Bluetooth on Linux, use `smpclient` through BlueZ so the client reuses
the authenticated bond. Upload `zephyr/zephyr.signed.bin`, never the unsigned binary.

When Wi-Fi and MCUboot are enabled, upload the same signed image from the settings page or with
`curl`, then restart the device to install it:

```sh
curl --fail --data-binary @zephyr.signed.bin \
  -H "Content-Type: application/octet-stream" \
  -H "X-OSKey-Request: 1" \
  http://DEVICE/firmware
curl --fail -X POST -H "X-OSKey-Request: 1" http://DEVICE/reboot
```
