# Combined Sessions + BLE: flash validation

Branch: `specter/ble-flash-validation` in `Anon0ne/Specter`.

This records the original combined-build validation. Subsequent touchscreen
and radio-handoff work is documented in [BLE_TOUCHSCREEN.md](BLE_TOUCHSCREEN.md)
and [WIFI_BLE_HANDOFF.md](WIFI_BLE_HANDOFF.md).

## Preserved input

The build was found in the `specter-ble-suite` worktree on `hale/ble-suite`, based
on `3f8ffe0`. Its four uncommitted files (MenuFunctions.cpp/.h and bt_core.cpp/.h)
were copied verbatim into the isolated validation clone and committed as `074525e`.
Claude's worktree, checked-out branch, index and uncommitted edits were not changed.

The original artifact (`ble_bin/esp32_marauder.ino.merged.bin`) was confirmed to
contain the Specter BLE launcher and command router. Its compiled source copies
matched the live working files. It was not flashed unchanged after the storage
and scan-duration problems below were found.

## Corrections

- Include project `configs.h` in bt_core.cpp so HAS_SD is visible to this
  translation unit and its session/SD blocks are actually compiled.
- Initialize the BLE storage backend with the mounted SD filesystem.
- Replace LittleFS's format-on-failure mount with a non-formatting mount.
  Unavailable fallback storage now fails without formatting the settings partition.
- Pick an unused BLE log filename, including across reboots, instead of truncating
  a previous `<module>_1.jsonl` file when the sequence counter restarts.
- Refuse a session log if its session-index registration fails.
- Match NimBLE 2.x scan duration units (milliseconds), retaining the seconds
  conversion only for the older API. Report a failed scan start; use a wrap-safe
  software timeout check.
- Tracker's logging status reflects whether a log file was actually opened.

## Build and flash

Core: esp32:esp32 3.3.4; NimBLE-Arduino 2.3.8; V8 H4W9 TFT_eSPI setup with the
hardware-confirmed display pins. Same FQBN as the working Specter images:
`esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled`.
NimBLE flags match Claude's two-connection/MTU-256/all-roles recipe.

`tools/build_v8_ble.sh` reproduces this recipe in a separate build directory and
stages its own TFT library. It never flashes or modifies the shared libraries.

The fixed build used 2,181,140 bytes (65%) of app storage and 100,772 bytes (30%)
global RAM. App image size: 2,181,296 bytes with padding.
SHA-256: `5fa597615d2343dd1135d4ddc872b5732adb593f21280d2ca4e929bcfed84fc6`.

Flashed the separate bootloader, partition, boot-app and app images via arduino-cli
on `/dev/ttyUSB0`; each hash verified. The full merged image was not written over
NVS. Device identified as ESP32-C5 rev v1.0.

## Device results

The serial check waited through startup, synced the clock, disarmed BLE, and ran:

1. `bt status`: armed=no, host=down, scan=off, tx=off, loot=closed.
2. `bt loot`: selected SD storage (`SD:/btloot`).
3. `tracker scan 5 log`: scan stayed active and opened
   `/btloot/tracker/tracker_1.jsonl`. It completed and closed the log; 10 received
   advertisement frames were counted.
4. Started **BLE validation test**, ran another five-second scan, and observed
   session storage under `/specter/sessions/s_918C7AA1_9FB67C9F/bluetooth/tracker_2.jsonl`.
5. Attempting `session stop` during the scan correctly returned
   `Stop capture/jobs before ending`.
6. The second scan completed (9 advertisement frames); session status reported
   one indexed file. The session then ended successfully.
7. `stopble` and final status confirmed disarmed, scan=off, tx=off and loot=closed.
   Serial was closed afterward so it cannot interfere with touchscreen testing.

No known tracker categories were detected in those brief scans. This verifies
scan operation and log-file creation/indexing/closure, not nonempty classified
tracker records. No advertising/spam, pairing, GATT writes or exploit actions
were exercised.

## Remaining integration work

- Live touchscreen BLE results via `bt_ui_get` remain unwired, as previously noted.
- The radio pre/post hook registration and switching between legacy Marauder BLE,
  BT+ and WiFi still require targeted integration testing.
- `bt_busy()` currently covers scanning/TX. Long-running GATT/client jobs should
  also be included before relying on the Sessions busy guard for those modules.
- Other modules, classification accuracy, repeated reconnect/deinit cycles and
  behavior on SD removal remain untested by this smoke test.

For handoff, this branch contains both the preserved launcher checkpoint and the
fixes. Avoid reapplying Claude's identical uncommitted launcher edits twice when
merging it back into `hale/ble-suite`.
