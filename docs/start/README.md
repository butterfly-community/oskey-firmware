<!-- SPDX-License-Identifier: MPL-2.0 -->

[中文](README_zh.md) · [Build Guide](compile.md) · [Product Gallery](../showcase/README.md)

# Quick Start

This guide follows the current touchscreen interface. Available pages, connections, and peripherals depend on your board and firmware configuration. The screenshots use demonstration data; never use a phrase shown in documentation for a real wallet.

## Prepare the Firmware

Build and flash your board using the [build guide](compile.md), or follow the instructions for the matching precompiled firmware in [Releases](https://github.com/butterfly-community/oskey-firmware/releases). The [ESP32 flash guide](../board/README_zh.md) is available in Chinese. For a device using MCUboot, follow the [signed firmware update guide](firmware-update.md).

On first boot, review **OSKey capabilities**, then select **Set up OSKey**. Boards without a display use a host application over the connections enabled in their firmware.

<img src="../showcase/images/02-capabilities.png" alt="Device capabilities and wallet setup entry" width="240">

## Create or Restore a Wallet

1. Create and confirm a wallet PIN. The device interface requires at least eight characters containing uppercase and lowercase letters, a number, and a symbol.
2. Choose **Generate recovery phrase** or **Import recovery phrase**.
3. For a new wallet, select **12 words**, **18 words**, or **24 words**, then choose the entropy method. Hardware randomness can be combined with touch, motion, camera, and microphone input when those modules are available. See [mnemonic entropy](../mnemonic-entropy.md) for details.
4. Write down the words offline and in order. Select **I saved the recovery phrase**, then enter the phrase again to verify it. To restore a wallet, enter your existing English recovery phrase in order on **Import wallet**.
5. Set an optional **Mnemonic passphrase**, or leave it empty. Confirm a nonempty passphrase to finish creating the wallet. When restoring, use the same passphrase as the original wallet.

| Wallet PIN | Recovery length | Restore a wallet |
| --- | --- | --- |
| <img src="../showcase/images/03-create-pin.png" alt="Create the wallet PIN" width="200"> | <img src="../showcase/images/07-mnemonic-length.png" alt="Choose 12, 18, or 24 words" width="200"> | <img src="../showcase/images/20-import-wallet.png" alt="Enter an existing recovery phrase" width="200"> |

Keep the recovery phrase and any mnemonic passphrase backed up offline. The wallet PIN unlocks this device; the mnemonic passphrase determines which wallet is derived from the recovery phrase.

The default software backend stores the encrypted seed on the device. Firmware configured for an NXP A5000 uses the secure element for seed storage and PIN authentication. See the [NXP guide](../security/nxp-a5000.md) before changing storage backends.

## Unlock and Connect

After restarting a device with a stored wallet, enter its PIN on **Unlock OSKey**. The home page provides **Hardware wallet**, **Passkeys**, and **Device settings**, according to the enabled modules.

| Unlock the wallet | Wallet home | Connection settings |
| --- | --- | --- |
| <img src="../showcase/images/24-unlock-wallet.png" alt="Unlock the stored wallet with its PIN" width="200"> | <img src="../showcase/images/23-wallet-home.png" alt="Wallet, passkeys, and device settings" width="200"> | <img src="../showcase/images/48-device-settings.png" alt="Device connection and maintenance settings" width="200"> |

Use the connection supported by your firmware and host application:

- **UART:** wallet communication through the board's configured serial port.
- **WebUSB:** browser or application communication when the USB wallet interface is enabled.
- **Bluetooth:** discover OSKey and complete authenticated pairing. Keep discovery active and connect using its current resolvable private address (RPA); never reuse a cached address. Keep **Address privacy** enabled.
- **USB FIDO2:** passkey and security-key operations when the FIDO2 module is enabled. The FIDO PIN is managed separately from the wallet PIN by the host's authenticator tools.

## Review Signing Requests

A connected wallet application can request HD address derivation and Ethereum message or transaction signatures. Review the requested private-key access and signing details on the device before approving. Depending on the board, confirmation uses touch or the configured user button.

| Derived address | Message review | Transaction review |
| --- | --- | --- |
| <img src="../showcase/images/26-hd-account.png" alt="HD derivation path and address QR code" width="200"> | <img src="../showcase/images/28-ethereum-message.png" alt="Review an Ethereum message" width="200"> | <img src="../showcase/images/32-ethereum-transaction.png" alt="Review an Ethereum transaction" width="200"> |

The [product gallery](../showcase/README.md) also illustrates FIDO2 registration and authentication, QR interactions, audio, motion sensing, and SD card browsing.

## Wi-Fi and Browser Settings

With Wi-Fi enabled, open **Device settings → Wi-Fi** to scan networks, save credentials, and view the current local IP address. You can also join the device's provisioning access point and open its displayed address. The defaults are **OSKey-AP**, password `12345678`, and **http://192.168.4.1**; firmware configuration can change them.

The device's local settings page provides Wi-Fi credentials, hostname, and restart controls. Firmware built with MCUboot also provides signed firmware upload. See [firmware updates](firmware-update.md) for image selection and installation.

| Device Wi-Fi status | Browser Wi-Fi setup | Browser device settings |
| --- | --- | --- |
| <img src="../showcase/images/54-wifi-connected.png" alt="Wi-Fi connection and local IP address" width="200"> | <img src="../showcase/images/86-browser-wifi.png" alt="Configure Wi-Fi in the local browser page" width="200"> | <img src="../showcase/images/87-browser-device.png" alt="Configure the hostname or restart the device" width="200"> |

## Restart or Erase

Open **Device settings** and select **Restart** to reboot with saved settings. To initialize a different wallet, back up the recovery phrase and mnemonic passphrase, select **Erase wallet**, and confirm **Erase data** on the device. This removes wallet data, passkeys, and network settings, then restarts setup without requiring a firmware reflash.

<img src="../showcase/images/78-reset.png" alt="Confirm erasing wallet, passkeys, and network settings" width="240">

If **Storage unavailable** appears, use **Restart** first. The storage recovery page also offers an erase action. NXP wallet erasure and PIN attempt limits are described in the [NXP guide](../security/nxp-a5000.md).

## Development and Resources

- [Linux native simulator](linux-development.md)
- [Signed firmware updates](firmware-update.md)
- [Lichuang audio and microphone](../board/esp32s3-lichuang-audio.md)
- [Lichuang SD card support](../board/esp32s3-lichuang-sd-card.md)
- [IMU coordinates and orientation](../board/imu-coordinate-system.md)
- [Internal storage](../storage.md)
- [Screenshot manifest](../showcase/manifest.json) and [capture tool](../../src/tests/showcase/README.md)

| WeChat | Telegram |
| --- | --- |
| <img src="../image/start/wechat.jpg" alt="OSKey WeChat community QR code" width="150"> | <img src="../image/start/tg.png" alt="OSKey Telegram community QR code" width="200"> |
