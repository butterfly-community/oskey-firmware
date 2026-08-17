<!-- SPDX-License-Identifier: MPL-2.0 -->

# Patch status

Last audited on 2026-08-17 against:

- Zephyr `5058917ea61b9d075f24c0dfeb89fdb95a405d52`
- Zephyr Rust module `ab3e546232bfa2f4e16d6c5156b9d9500885b4e8`

Every retained patch applies cleanly at those revisions and is exercised by at least one OSKey
build configuration.

| Patch | Why it is still required |
| --- | --- |
| `tf-psa-threading.patch` | Enables the TF-PSA global mutex and supplies its Zephyr mutex backend. Concurrent wallet, FIDO, and networking callers otherwise share an unprotected PSA key-slot state. |
| `esp32-virtual-efuse.patch` | Includes the HAL declaration used by Zephyr's virtual-eFuse flash-encryption path. Without it, the documented MCUboot virtual-eFuse configuration fails with an implicit-function-declaration error. |
| `fido/change-pin.patch` | Implements the still-missing CTAP `changePIN` subcommand. It mirrors Zephyr PR [#115238](https://github.com/zephyrproject-rtos/zephyr/pull/115238) and can be removed after that PR, or an equivalent implementation, lands. |
| `fido/oskey.patch` | Connects Zephyr's FIDO core to OSKey wallet-backed credential creation, validation, signing, authorization context, and reset policy. Zephyr's custom UP, storage, and attestation backends do not provide an equivalent credential-crypto hook. |
| `fido/usb-busy.patch` | Keeps a CBOR channel active until core processing completes, rejects concurrent channels, and scopes CANCEL to the active channel. Upstream only tracks packet reassembly and cancels an in-flight request when another request arrives. |
| `fido/keepalive.patch` | Runs native_sim USB/IP keepalive work on the HID receive queue so simulated USB access stays on its required execution context. |
| `usb-dwc2-zlp-cache.patch` | Avoids forwarding a zero-length IN transfer to the cache-maintenance backend. The ESP cache driver still lacks a zero-length guard. |
| `rust.patch` | Adds esp-rs Xtensa targets and `build-std`, makes generated configuration usable on 32-bit targets, and routes large Rust allocations to ESP PSRAM. Upstream's Rust module does not support Xtensa or this allocator policy. |

The following patches were removed by this audit:

- `lvgl-native-sim-benchmark.patch`: OSKey now supplies LVGL with Zephyr Native Simulator's
  pseudo-host real-time clock through `lv_tick_set_cb()`, so LVGL itself needs no modification.
- `video-rgb565x-bpp.patch`: OSKey's GC0308 driver sets its RGB565X pitch and buffer size directly;
  the project does not rely on Zephyr's generic bits-per-pixel helper for this capture path.
