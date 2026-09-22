<!-- SPDX-License-Identifier: MPL-2.0 -->

# Patch status

Last audited on 2026-09-22 against the main revisions pinned in [`west.yml`](../west.yml):

- Zephyr `0388ef0c424d70302c279d38f0aa1a69e92e157d`
- Zephyr Rust module `6889ba3bdc0d9debfbb5aea07c1a869935eea65b`

Toolchains: Zephyr SDK 1.0.1, Rust 1.97.1, and esp-rs 1.95.0.0. The Dockerfile uses
Zephyr CI image v0.29.4 and installs those Rust versions explicitly.

Every retained patch applies cleanly at those revisions and is exercised by at least one OSKey
build configuration.

| Patch | Why it is still required |
| --- | --- |
| `tf-psa-threading.patch` | Enables the TF-PSA global mutex and supplies its Zephyr mutex backend. Concurrent wallet, FIDO, and networking callers otherwise share an unprotected PSA key-slot state. |
| `esp32-virtual-efuse.patch` | Includes the HAL declaration used by Zephyr's virtual-eFuse flash-encryption path. Without it, the documented MCUboot virtual-eFuse configuration fails with an implicit-function-declaration error. |
| `fido/oskey.patch` | Connects Zephyr's FIDO core to OSKey wallet-backed credential creation, validation, signing, authorization context, and reset policy. Zephyr's custom UP, storage, and attestation backends do not provide an equivalent credential-crypto hook. |
| `fido/usb-busy.patch` | Keeps a CBOR channel active until core processing completes, rejects concurrent channels, and scopes CANCEL to the active channel. Upstream only tracks packet reassembly and cancels an in-flight request when another request arrives. |
| `fido/keepalive.patch` | Runs native_sim USB/IP keepalive work on the HID receive queue so simulated USB access stays on its required execution context. |
| `usb-dwc2-zlp-cache.patch` | Avoids forwarding a zero-length IN transfer to the cache-maintenance backend. The ESP cache driver still lacks a zero-length guard. |
| `rust.patch` | Adds esp-rs Xtensa targets and `build-std`, makes generated configuration usable on 32-bit targets, and routes large Rust allocations to ESP PSRAM. Guards generated GPIO key accessors with `CONFIG_GPIO` so GPIO-disabled builds remain valid. Upstream's Rust module does not yet provide these fixes. |

Removed patches:

- `fido/change-pin.patch`: upstream commit `13c6f3432012f48dbe90bf8be64954e6f226709e`
  implements CTAP `changePIN` and adds a decrypted PIN-hash length check to the previous
  implementation; use the upstream implementation.
- `lvgl-native-sim-benchmark.patch`: OSKey now supplies LVGL with Zephyr Native Simulator's
  pseudo-host real-time clock through `lv_tick_set_cb()`, so LVGL itself needs no modification.
- `video-rgb565x-bpp.patch`: OSKey's GC0308 driver sets its RGB565X pitch and buffer size directly;
  the project does not rely on Zephyr's generic bits-per-pixel helper for this capture path.

## Reproduce the workspace

OSKey's manifest imports all dependencies from the pinned Zephyr revision and overrides the
Rust module with its separately validated revision. Do not update individual HAL or crypto
modules to unrelated branch heads.

With west and its Python environment installed, create a workspace:

```sh
mkdir zephyr-project
cd zephyr-project
git clone --recursive https://github.com/butterfly-community/oskey-firmware oskey
west init -l oskey
west config manifest.group-filter -- +optional
west update
west blobs fetch hal_espressif
python3 oskey/patch/apply.py "$PWD"
```

For an existing workspace, preserve local changes before updating. Remove the current patches
with `python3 patch/apply.py "$ZEPHYR_WORKSPACE" --reverse`, using the script and patches from
the current version of OSKey, before switching to a new OSKey revision. For older checkouts
without this helper, save tracked and untracked changes in both `zephyr` and `modules/lang/rust`
(for example with `git stash push -u`) before upgrading. Do not restore the old patch stack
over the new one: some changes are now upstream.

Set `ZEPHYR_WORKSPACE` to the workspace directory and run from the updated OSKey source directory
after loading the development environment:

```sh
west config manifest.path "$(realpath --relative-to="$ZEPHYR_WORKSPACE" "$PWD")"
west config manifest.file west.yml
west config manifest.group-filter -- +optional
west update
west blobs fetch hal_espressif
python3 patch/apply.py "$ZEPHYR_WORKSPACE"
```

The script verifies both pinned revisions before modifying files and skips patches already in
the requested state. Once the workspace matches the pinned revisions, repeating `west update`
does not require removing the patches.

The Rust patch was rebased around upstream's Hexagon and blocking-pool additions. The
application now explicitly links upstream's separate `zephyr-panic` crate.

## Validation

- All 71 active west projects match their manifest revisions.
- Patch application, repeated application, removal, and reapplication succeed.
- ESP32-S3 and native_sim builds cover NXP enabled and disabled; additional builds cover
  FIDO/USB, MCUboot virtual eFuses, and the display/audio/camera/IMU configuration.
- ARM `nucleo_f401re` builds with GPIO disabled, exercising the Rust devicetree guard.
- The complete `build.ts` release matrix passes: all 14 ESP32, ESP32-S3, STM32, and nRF52840
  configurations build successfully.
- NXP: 5 C suites pass normally and with ASan/UBSan; Rust adapter: 1 test passes.
- PSA action tests: 46 pass with `--test-threads=1`; integration runner: 2 tests pass.
- native_sim UART: 20 wallet/protocol tests pass with NXP disabled; 7 protocol tests pass
  with NXP enabled and another 7 pass with the FIDO/USB/display configuration.
