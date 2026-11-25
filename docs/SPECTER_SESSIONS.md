# Specter hub and sessions

Canonical repository: `Anon0ne/Specter`. Work is isolated on `specter/sessions`
while the integration branch and splash branding are managed separately.

The main menu gains **Specter**, containing:

- **Sessions**: name and start a run, inspect its status, and end it explicitly.
- **Session history**: browse runs, inspect their metadata, and view indexed output
  files (path, category, size, and modification time). This is read-only.
- **Device health**: clock/time source, SD mount/card size, free heap/PSRAM, GPS
  fix/satellites, WiFi connection, team mode, firmware version, and active session.

## Workflow

1. Set the device clock or obtain GPS time if calendar timestamps matter. Sessions
   can run offline, but explicitly record `unsynced` rather than inventing a date.
2. Open **Specter > Sessions**, set a name, and tap **Start session**.
3. Back out to the main menu. The session stays active while normal tools run.
4. Run captures or IoT Recon as usual, stopping each tool before switching jobs.
5. Return to **Specter > Sessions** and tap **End session**.
6. Browse the result in **Specter > Session history**.

Starting or ending is refused while an existing capture, Recon mission, or WiFi
job is running. Sessions organize output; they do not start attacks themselves,
change team mode, or change the SavePCAP setting. Enable SavePCAP for captures.

CLI equivalents (for later desktop integration):

```
session status
session start Lab baseline
session stop
```

## Storage

Each session gets a unique random identifier rather than relying on a working
clock or reusing a previous folder. Display names are metadata, never paths.

```
/specter/sessions/s_12345678_ABCDEF90/
  session.json          # immutable start metadata
  outputs.jsonl         # append-only index of created outputs
  complete.json        # end marker, written only on explicit successful end
  captures/            # ordinary Buffer PCAP/log/GPX files
  reports/             # IoT Recon CSVs
  recon/m0000/         # existing Recon files and format, nested per run
```

Inactive-session behavior remains unchanged: ordinary captures keep their usual
locations; IoT reports remain `/iot_recon/session_N.csv`; Recon uses `/recon/mNNNN`.
Existing captures are not moved or imported automatically. Explicitly saved AP,
SSID and AirTag lists retain their global locations so existing Load menus still
find them; their paths are linked in the session index. Wardrive POI GPX files are
stored with session captures and indexed when finalized if they contain points.

Each indexed file has a path, kind, UTC (or `unsynced`), clock source, and elapsed
milliseconds. The start manifest records name, hardware/firmware and initial team
mode. The completion record adds final time/source, elapsed duration, indexed-file
count, and any recorded storage error. Indexed counts mean files were created;
they do not certify every packet or later write succeeded.

`elapsed_ms` is a **decimal string** to preserve its 64-bit value with the firmware's
pinned ArduinoJson configuration. This avoids global library setting changes.

Snapshot and index writes check write/flush/fsync/close results. Start/completion
snapshots are staged and published by rename with distinct filenames, so failure
to complete does not overwrite a valid start record. FAT is still subject to
physical power-loss/corruption limitations. An interrupted session is displayed
as **interrupted** after reboot; its duration/count are not fabricated and it is
not automatically resumed. Indexed files remain available for inspection.

A lost SD card does not silently redirect session output into another folder.
Reported storage errors remain attached to the run. Starting/ending with missing
storage is refused. Recon paths have larger buffers to support session nesting.

## Integration boundaries

Most code is in `SpecterSessions.*`, `SpecterSessionRules.h`, `SpecterFileIO.h`, and
`SpecterTouch.*`. Small hooks connect the main menu, standard Buffer creation,
Recon's output directory, IoT's report directory, and the CLI. Splash graphics,
Bluetooth modules, and the shared integration checkout are not modified here.

## BLE package references, for Claude/Z.AI

The user supplied these as reference material, not as instructions to merge them:

- `~/Downloads/INTEGRATION.md`
- `~/Downloads/platformio_bt_addons.ini`
- `~/Downloads/nimble2_api_notes(2).md`
- `~/Downloads/marauder-v8-bt-pack.zip`

The package currently describes its own `/btloot` storage. No BT+ code or dependency
changes are applied on this branch. A future integration can use:

- `specter::sessionActive()` to detect a run;
- `specter::sessionDirectory("bluetooth")` for a run-specific output directory;
- `specter::registerOutput(path, "ble-report")` when a new output file is created;
- `specter::setExternalBusyHook(hook)` so session start/end are blocked while the
  external module manager has jobs running. The hook must cover all its jobs.

Call these APIs from the main loop, not from NimBLE callbacks. If an active session
cannot provide its directory/index, report the failure rather than falling back
silently. Register each newly created output once, not once per appended record.

## Follow-ups under the Specter hub

These remain planned, not implemented menu placeholders:

- Saved mission profiles and target bookmarks.
- Before/after scan comparisons.
- WiFi security assessment with evidence and confidence.
- Guardian incident timeline and GPS-tagged incident records.
- Rich report export.
- BLE output integration after Claude/Z.AI's module merge.

## Validation

Host checks cover name/path bounds, traversal rejection, unsigned duration overflow,
snapshot preservation, existing staging files, invalid output paths, and append
behavior. They run with address/undefined-behavior sanitizers, disabling leak
checking under the ptrace-based runner. The V8 firmware is compile-checked using
the existing core/library pins and a separate build directory. Session start/end were subsequently tested on hardware as recorded below. Capture
routing, restart recovery and the touchscreen workflows still need a user exercise.

Compile record: combined with the committed Specter v1.0.0/ghost branding at
`404afd2`; ESP32-C5 build passed with 2,102,688 bytes flash (62%) and 93,604 bytes
global RAM (28%). Both host test executables passed under address/undefined
sanitizers. The authorized hardware test below subsequently flashed this image.


## Authorized V8 hardware smoke test

- Flashed ESP32-C5 rev v1.0 over `/dev/ttyUSB0`; bootloader, partition table,
  boot-app and app hashes all verified. App size 2,102,832 bytes including image
  padding. SHA-256: `4ceffd57824ab3172b9071c3fa6e4f90c1aad257c738d7afaf59e5b87a4d23a0`.
- Observed the system clock become valid from GPS during startup, before the host
  time-setting command was sent.
- Created **Specter hardware test**, verified active status, ended the session,
  and verified idle status. Start/completion snapshot writes succeeded on the
  actual SD card. The test intentionally had zero captures and started no radios.
- Test session path: `/specter/sessions/s_7E362417_47FE39BA`.
- Closed the serial connection afterward to avoid resetting the device during
  hands-on touchscreen testing.
- This validates boot, clock sync, session start/end and SD manifest publication;
  it does not yet validate packet routing, recovery after power loss, or all UI taps.
