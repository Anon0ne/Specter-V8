# WiFi automation and SD dates

Originally implemented on `hale/wifi-automation` and now included in the combined
Specter build. These additions have touchscreen controls and CLI commands;
desktop GUI controls remain a later integration step. CLI commands use the
existing serial connection. See [FEATURE_MAP.md](FEATURE_MAP.md) for current coverage.

## Why files showed 1970

The board previously stored the GPS date as a string but never set the system
clock. ESP32 FAT uses system time for modification dates. There is no battery-backed
wall clock in this implementation; a full power loss requires a fresh time source.

- Valid GPS fixes now synchronize the system clock in UTC, at most once a minute.
- Without GPS: `clock set <Unix UTC seconds>` sets the clock from the host.
- `clock` reports UTC, source, and epoch, or explicitly reports `unsynced`.
- Run `python tools/sync_device_clock.py --port /dev/ttyUSB0` from an environment
  with pyserial. Close any GUI/serial monitor holding the port first.
- New writes/closed files use the synchronized system clock. FAT has two-second
  timestamp precision and no timezone field; UTC is the firmware convention.
- Existing 1970 dates cannot reveal the original creation time. Nothing rewrites
  those dates automatically or pretends uptime is a calendar date.
- If a file's real date is known, use `filedate <Unix UTC seconds> </file/path>`.
  This changes metadata only. Example host helper:
  `python tools/sync_device_clock.py --file /capture.pcap --date 2026-11-01T18:00:00-05:00`.
  `--dry-run` shows commands without opening a port. The V8 SD mount is `/sd`.

## Probe-to-portal

1. Configure an existing Evil Portal HTML template on SD using the normal workflow.
2. Stop the current scan and run `probetwin start My Lab Network`.
3. The device listens for a directed probe matching that exact SSID. Wildcard probes
   and unrelated SSIDs do not trigger it. SSIDs are case-sensitive, 1..31 bytes
   (the existing portal stores a 32-byte C string).
4. After a match, the main loop stops sniffing and requests the existing portal.
   The WiFi callback only sets a flag under a critical section; it never changes
   radio modes, allocates strings, or touches files.
5. `probetwin status`, `probetwin stop`, and `stopscan` provide control.
   Listening expires after 60 seconds. Changing scan mode cancels the pending job.

The existing Evil Portal provides the page and any collection behavior. This
workflow adds a transition, not a new credential collector. Claude's Red/Blue team gate is integrated: Blue Team rejects arming, and switching
to Blue Team cancels a pending or active probe-to-portal workflow. The existing
StartScan gate also checks the portal transition. IoT discovery remains available
in either team mode.

## IoT Recon: initial discovery/reporting pass

Join the test LAN using the normal WiFi join workflow, then:

```
iotrecon start 192.168.1.1 192.168.1.20
iotrecon status
iotrecon stop
```

The chosen range must be on the current subnet, contain at most 254 addresses,
and exclude network/broadcast addresses. The scanner skips its own address. It
checks FTP, SSH, Telnet, HTTP, HTTPS, SMB and MQTT conventional TCP ports
(21,22,23,80,443,445,1883,8080,8883).

One connection attempt is scheduled per tick, with a 150ms connect timeout and
100ms pacing. This keeps serial stop/status processing between attempts; it is
not a background task. Network changes cancel the session. Results distinguish
`open` from `closed-or-filtered`; a failed connection does not prove a host absent.
Port names are conventions, not verified service/device fingerprints.
The automation is enabled by `HALE_WIFI_PROBE_TWIN` and `HALE_IOT_RECON`;
setting either to zero makes its command reject requests.

Each session creates a new `/iot_recon/session_N.csv` without overwriting an
existing file. Reports include UTC (or `unsynced`), uptime, address, port, result,
and final status. They flush after each host and on stop/completion. SD failures
cancel rather than reporting a successful saved session.

Not implemented in this pass: credential guessing, banner/version fingerprinting,
UDP discovery, vulnerability verdicts, or desktop GUI controls for these jobs.

## Build and hardware checks

`tools/build_v8_automation.sh /tmp/marauder-v8-automation` uses the installed core
3.3.4 and libraries. It stages its own TFT library copy and uses a separate build
folder; it never flashes or changes the shared Arduino libraries.

Host checks: compile `tests/automation/core_test.cpp` with `-Iesp32_marauder`,
`-fsanitize=address,undefined`, linking `IPv4Range.cpp`, and run with `ASAN_OPTIONS=detect_leaks=0` under
ptrace-based sandboxes. Hardware checks still required:

- Cold boot without GPS: `clock` must say unsynced, then host sync and new-file
  writes should have the correct modification date.
- GPS fix: UTC should synchronize; verify an SD write and host-reported mtime.
- Apply a known date to a test file; verify content and size unchanged.
- Probe match, nonmatch, timeout, stop, repeated starts and template failure.
- IoT known open/closed ports, short range, stop, reconnect and SD removal.
- Blue Team must block probe-to-portal; Red Team permits it.

## Validation record

- Host UTC/calendar, epoch overflow, subnet bounds, and malformed/truncated probe
  checks passed with address and undefined-behavior sanitizers (leak detection
  disabled because the runner uses ptrace).
- Host clock-helper tests passed for explicit timezone conversion, spaces in file
  paths, missing dates/timezones, and path/line-break rejection.
- First automation build passed on ESP32-C5: 2,101,830 bytes, 91,348 bytes globals.
- Integrated Claude's team-mode commit (`b3b1a14`) in the isolated clone;
  integration build passed: 2,104,344 bytes, 91,372 bytes globals.
- Final build including feature flags, cancellation timing, and SD write/flush/fsync
  checks passed: 2,104,566 bytes flash (62%), 91,364 bytes globals (27%).
- The CLI automation checkpoint above was compile-tested before flashing. See the
  touchscreen validation notes below for the subsequent device test.

## Touchscreen controls

- **WiFi > Sniffers > Probe to Portal:** set an exact SSID with the touch keyboard,
  choose an existing `.html` file from the SD browser, and tap Start. The display
  shows waiting/portal status, Red/Blue mode, and errors. Stop or Back stops the job.
  Starting from a joined LAN leaves that connection to switch into probe sniffing.
- **WiFi > Scanners > IoT Recon:** join the test LAN first, edit the proposed first
  and last IPs if necessary, then Start. Current address, check/open-port counters,
  report filename, completion and error status remain visible. Stop or Back stops
  the job and closes its report.
- **Device > Clock / File Dates:** displays UTC and the time source. Set UTC manually
  using `YYYY-MM-DD HH:MM:SS`, or let a valid GPS fix set it automatically.
- **Browse SD file dates:** navigate folders and choose a file, enter a known UTC
  date, review it, then explicitly tap Apply. No automatic guessing of old dates.
- **Test SD dates:** creates a uniquely named temporary test file, checks its new
  modification date, changes the timestamp by an hour, verifies the contents, then
  removes it. `clock sdtest` provides the same test over serial.

The dashboards handle taps on release and require the previous touch to clear
before accepting another action. Normal sniffer touch controls and scrolling text
are suppressed while an automation screen owns the display. Editing job settings
requires stopping the job; a portal transition is handled outside the WiFi callback.

### Touchscreen build and device validation

- V8 build passed: 2,116,710 bytes program storage (63%), 91,836 bytes globals (28%).
- Flashed ESP32-C5 rev v1.0 on `/dev/ttyUSB0`; bootloader, partitions, boot-app,
  and app hashes all verified. App binary SHA-256:
  `913164eb0071f29c441a6141ecdb0e26e88dee9d3b4ade6d95f892e6c3b4211a`.
- User confirmed that the screen boots and the new feature entries appear.
- Serial checks passed: clock status, host time synchronization, probe idle,
  and IoT idle. `clock sdtest` passed on the real SD card: created timestamp
  `1793586740`, applied timestamp `1793583140`, contents unchanged, test file removed.
- User confirmed SD browsing no longer resets with the test serial connection
  closed, and file dates look correct. Opening/closing that serial connection can
  reset this board; do not reconnect a monitor during hands-on touchscreen testing.
- Still requiring functional exercise: an exact directed probe causing the portal
  transition, live IoT service discovery/report inspection, and touch-keyboard
  edits/cancel paths. Build and timestamp checks do not substitute for those tests.
