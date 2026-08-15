# Multi-Source Entropy for Mnemonic Generation

## Goals and Security Boundary

Multi-source mode supplements hardware-generated randomness with data collected from touch
gestures, IMU samples, camera frames, and microphone PCM. The hardware CSPRNG always
participates in the final derivation, so a fixed, predictable, or skipped sensor source cannot
weaken the security of the existing random generation mode. Multi-source mode is unavailable
when no hardware CSPRNG is present.

Exact custom entropy remains a separate mode. In that mode, the user enters all 128, 192, or
256 bits directly, without mixing in hardware randomness or sensor data.

The implementation introduces no new Kconfig options. Available sources are determined by the
existing `OSKEY_DISPLAY`, `OSKEY_IMU`, `OSKEY_CAMERA`, `OSKEY_MICROPHONE`, and `OSKEY_RUST`
options together with the runtime state of each device.

## User Interface Flow

During wallet creation, the user first selects a 12-, 18-, or 24-word mnemonic and then chooses
an entropy method:

1. **Hardware randomness** uses the existing mnemonic generation request.
2. **Multi-source enhanced** mixes hardware randomness with selected sensor sources. It is shown
   only when a hardware CSPRNG and at least one auxiliary source are available.
3. **Exact custom entropy** opens the existing bit-entry interface.

The source selection page initially enables every source supported by the device. The user may
select any combination or use only one auxiliary source. Selected sources are collected
sequentially in source-bit order to avoid resource and interaction conflicts between the camera,
audio, and IMU subsystems.

The collection page shows progress for the active source. A failed source can be retried or
skipped, and an active source can also be skipped manually. Leaving the page cancels the current
session and discards its intermediate digests.

## Source Collection

Each source uses an independent PSA SHA-256 operation. The hash input begins with the domain
separator `OSKEY/ENTROPY-SOURCE/V1`, followed by the protocol version and source identifier.
Each data batch then contributes its elapsed collection time, sample count, and raw bytes.

Only the resulting 32-byte digest is added to the transcript. Photos, recordings, touch paths,
and raw IMU samples are never written to persistent storage. Capture buffers continue to follow
the ownership rules of their existing device drivers.

| Source | Input | Completion condition |
| --- | --- | --- |
| Touch | Canonical little-endian `x`, `y`, `dx`, and `dy` values | At least 64 consecutive, distinct position events |
| IMU | Canonical integer encoding of three-axis acceleration and angular velocity | At least 256 samples |
| Camera | Raw 320×240 RGB565 frames | 8 frames, approximately 200 ms apart |
| Microphone | Stereo 16-bit I2S PCM blocks | At least 48,000 frames |

The camera service uses an atomic occupancy flag so QR scanning and entropy collection cannot
start the video stream simultaneously. The IMU source reuses the existing sampling thread and
start/stop commands. Microphone entropy collection shares the existing I2S capture thread with
the USB microphone service.

## Transcript Format

The collection service submits a canonical transcript to the Rust core. All multi-byte integers
are little-endian.

| Offset | Length | Description |
| --- | ---: | --- |
| 0 | 4 | Magic value `OSEM` |
| 4 | 1 | Format version, currently `1` |
| 5 | 1 | Mnemonic word count: 12, 18, or 24 |
| 6 | 1 | Selected-source bitmap; hardware randomness must be included |
| 7 | 1 | Completed auxiliary-source bitmap |
| 8 | 1 | Skipped auxiliary-source bitmap |
| 9 | 1 | Number of source records that follow |
| 10 | 41 × N | Source records |

Each source record contains a one-byte source identifier, a four-byte sample count, a four-byte
collection duration, and a 32-byte SHA-256 digest. Records must be strictly ordered by source
identifier and must not contain duplicates. The completed and skipped bitmaps must be disjoint
and together cover every selected auxiliary source. The Rust core validates the entire structure
before requesting hardware randomness.

## Final Mixing

For every mnemonic generation request, the Rust core obtains a fresh 32-byte CSPRNG value `R`
and treats the complete transcript as `T`:

```text
PRK = HMAC-SHA512("OSKEY/MNEMONIC-ENTROPY/V1", R || T)
OKM = HMAC-SHA512(PRK, "OSKEY/BIP39/V1" || LE32(words) || 0x01)
```

The implementation takes the first 16, 24, or 32 bytes of `OKM` to generate a 12-, 18-, or
24-word BIP-39 mnemonic. The domain separators, word count, and protocol version bind the output
to this specific purpose and format. Hardware randomness, intermediate keys, derived entropy,
the C-side transcript, and source digests are explicitly cleared after use.
