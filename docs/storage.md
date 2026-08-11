# Internal storage

OSKey uses Zephyr Memory Storage (ZMS) for persistent internal state. The same
ZMS instance contains two independent groups of records:

- application records with stable low numeric IDs: the wallet seed, unlock
  failure counter, and firmware-update request;
- Zephyr Settings records used by Wi-Fi, Bluetooth, FIDO2, and other settings
  handlers.

Application code accesses the first group only through `src/storage.c`.
Settings users continue to use the Zephyr Settings API and must not access the
underlying ZMS IDs directly. Removable FAT/exFAT media is separate and never
exposes internal ZMS records through the file browser.

| Application ID | Record |
| ---: | --- |
| 2 | Wallet seed |
| 3 | Unlock failure counter |
| 4 | Firmware-update request |

The low application-ID range is reserved for `src/storage.c`; new callers must
not allocate IDs independently.

## Configuration

`CONFIG_OSKEY_STORAGE` selects ZMS, Zephyr Settings, the flash map, and ZMS data
CRC. ZMS metadata is protected by its allocation-entry CRC, while
`CONFIG_ZMS_DATA_CRC` verifies record contents when the complete value is read.
The storage wrapper therefore rejects a read buffer smaller than the stored
record instead of returning truncated data.

Lookup caches, duplicate-write scans, proactive garbage collection, and forced
mount are intentionally not enabled. The current data set is small and writes
are infrequent, while ZMS already handles sector rotation. A damaged wallet
partition must not be silently formatted.

Enabling ZMS data CRC changes the on-flash format. Existing storage is not
migrated; erase the storage partition or the whole flash once before first boot
with this configuration.

## Startup and reset

Startup has two phases:

1. `storage_init()` initializes Zephyr Settings, mounts its ZMS backend, and
   publishes the ready or error state.
2. After modules have registered their Settings handlers,
   `storage_settings_load()` loads the stored settings.

A missing application record is a normal result and does not change the global
storage state. Other ZMS failures publish `APP_STORAGE_ERROR` and stop further
access through the wrapper.

`storage_erase_flash()` clears the shared ZMS partition, including the seed,
failure counter, firmware-update request, and all Zephyr Settings. The caller
reboots immediately after a successful clear; the next boot formats the empty
partition normally.

## Storage API

- `storage_exists()` returns `1`, `0`, or a negative error code.
- `storage_read()` returns the complete record length or a negative error code;
  it returns `-EMSGSIZE` rather than truncating a record.
- `storage_write()` returns `0` or a negative error code.
- `storage_delete()` removes a record and returns `0` or a negative error code.

ZMS CRC detects corruption but does not encrypt or authenticate stored data.
Seed confidentiality still depends on the platform security configuration,
including secure boot and flash encryption where available.
