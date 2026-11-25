> Public beta limitation: stock C5 raw-frame validation is retained. Raw-transmit
> features that depended on patched-driver behavior may be unavailable. Menu or
> CLI coverage in this historical inventory does not establish radio acceptance.

# Specter feature map

Inventory for `specter/ble-touchscreen`, checked 2026-11-02 against source and
recent V8 tests. **Present** means an implementation and interface exist, not
that every device/protocol combination is validated. Menus can depend on build
flags, detected hardware, mounted SD and team mode. **Partial** means the
listed interface omits part of the underlying module.

## Main menu

| Entry | Purpose |
| --- | --- |
| Specter | Sessions, history and device health. |
| Recon | WiFi and BLE observation missions. |
| WiFi | Sniffers, scanners, assessment modes and network/list utilities. |
| Bluetooth | Legacy sniffers/assessment tools and the Specter BLE suite. |
| GPS | Data, NMEA stream, tracker and POI tools when GPS is detected. |
| Device | Updates, settings, storage, team mode, clock/dates and geofences. |
| Reboot | Restart the device. |

## Specter additions and workflows

| Feature | Touchscreen | Serial / storage | Validation or remaining work |
| --- | --- | --- | --- |
| Sessions | Start/end, history, file details, health | `session`; manifests/output index | Start/end and SD workflow tested. Output indexing is not a guarantee every write succeeded. |
| Deauth Guardian | Sniffers entry and alert state | Existing alert output | Core and device integration tested; structured Session/GPS alert logging remains. |
| AP tap actions | AP selection → Deauth / Clone | Per-AP command handlers | Previously tested on device; active actions require appropriate mode. |
| Probe to Portal | Automation panel | `probetwin`; existing portal/template | Exact-SSID transition, timeout and cancellation implemented; no new independent portal engine. |
| IoT Recon | Range, start/stop/status | `iotrecon`; CSV on SD | Conventional TCP-port discovery; no credential guessing or verified device fingerprinting. Post-ARP operation-state handoff is a known issue awaiting a separate fix. |
| WiFi/BLE Recon | Start missions and dashboards | `recon`; observation logs | WiFi AP retention after BLE verified; protocol/device coverage varies. |
| UTC clock and file dates | Clock / File Dates | `clock`, `filedate`, host helper | GPS/host synchronization and SD-date workflow tested; power loss requires re-sync. |
| SD File Browser | File navigation/actions | Existing SD commands | User-tested; device reset seen earlier was associated with serial testing. |
| BLE result modal | Live status, Stop/Back, cached results | `bt_ui_get()` | Host checks pass. Finished screens retain the last observed snapshot, not necessarily exact final totals. |
| GATT inspector | Predator Scan → Devices / Inspect | Existing recon table and JSONL | Host-tested picker/results flow; Chameleon Ultra GATT inspection passed with 4 services / 7 characteristics after address and MTU fixes. |
| BadBLE keyboard | SD picker, explicit Run, progress, Stop/Back | `bb`; DuckyScript subset and metadata JSONL | Manual hello verified by user on Android; SD script host tests pass, paired-host acceptance pending. |
| Blue/Red Team | Device setting | Persistent team setting | Gates designated offensive WiFi modes; BT+ also has a separate arm gate. |

## Existing WiFi inventory

| Group | Included tools / modes |
| --- | --- |
| Discovery and sniffing | Probe requests, beacons, deauthentication, AP/STA scan, EAPOL/PMKID, raw capture and SAE-commit observation. |
| Traffic views | Packet Count, Packet Monitor, Channel Analyzer and Channel Summary. |
| Detection and tracking | Pwnagotchi, Pineapple, MultiSSID, MAC Monitor and Fox Hunt target workflows. These depend on observable signatures/traffic. |
| Remote ID | Discovery, device list and selected-target view; transmitter compatibility requires validation. |
| LAN scanners | Ping, ARP, all configured ports, SSH, Telnet, SMTP, DNS, HTTP, HTTPS and RDP conventional ports. |
| Beacon/probe/auth tests | List/random/funny/Rickroll beacon modes and the existing Probe Req Flood/authentication path. |
| AP/client tests | Deauth flood/targeted, AP Clone Spam, Karma, Bad Msg/targeted, Assoc Sleep/targeted, SAE Commit Flood, Channel Switch and Quiet Time. |
| Portal | Existing Evil Portal, AP/SSID selection and SD HTML template selection; Specter adds exact-SSID Probe to Portal automation. |
| Network setup | Join WiFi, saved-network management, Start AP, network information and AP/STA MAC controls. |
| Lists and storage | Generate/add/select/clear SSIDs, AP/station selection and information, saved lists and capture output. |
| GPS/wardrive | Wardrive, GPS data, NMEA, tracking, POIs and configured log-upload support. Requires hardware/services as applicable. |

Upstream mode availability does not establish effectiveness against modern
protected networks. Dedicated GARMR enhancements and the proposed 85-bar auth
activity visualizer are not part of this inventory.

## Existing BLE inventory

| Group | Included tools / modes |
| --- | --- |
| Sniffers and detectors | Bluetooth Sniffer, Flipper, FindMy, iBeacon information, FindMy Monitor, legacy card-skimmer heuristics, Bluetooth Analyzer, Flock and Meta detectors. |
| Target tracking | BLE Fox Hunt target workflows for discovered device categories. |
| Advertisement test modes | Sour Apple, Apple Juice, Swiftpair, Samsung/Google/Flipper spam, combined spam and AirTag advertisement spoofing. |
| Conditional upstream entry | FindMy Sound exists behind the NimBLE 2 build flag; this inventory does not establish that it can trigger a genuine AirTag speaker. |

BLE-only discovery does not cover Bluetooth Classic devices.

## Specter BLE suite

All nine original modules have source files, enabled `BT_ENABLE_*` flags and
serial routing. Presence is distinct from tested results on target hardware.

| Module | Touchscreen coverage | Serial capability present | Status / limits |
| --- | --- | --- | --- |
| Tracker | Timed scan, live counters, Stop/Back | Scan, list, stop and optional logging | Discovery and SD logs smoke-tested; tracker-family classification needs known-device validation. |
| Phantom | Basic FindMy advertisement start/status | Additional replay/options | Partial touchscreen; advertisement behavior not field-validated. |
| Beacon | No dedicated launcher | iBeacon and Eddystone-style formats | Serial-only in the Specter suite. |
| Spoofer | Apple All and Swiftpair launchers | Apple variants, Android and timing options | Partial touchscreen; Android example model IDs are placeholders. |
| Predator | Scan, paged device selection, GATT result browser | Recon and honeypot implementations | Read-only inspector added; honeypot has no touchscreen workflow. Recon currently synchronous. |
| SkeletonKey | No dedicated workflow | GATT connection/dump, fuzz and LED handlers | Serial-only; not hardware-validated here. |
| WhisperPair | Scan/status only | Assessment probe handler | No target assessment screen; no full pairing or automatic CVE verdict. |
| KARR | Scan/status only | List, assessment and signature handlers | Generic signatures; no validated vulnerability verdict from a match. |
| Skimmer | Scan/status only | List and verification handler | BETA heuristic; target verification screen missing. |
| BadBLE | Paged SD scripts, manual Run, progress and Stop/Back | Serve/list/validate/run/type/key/status/stop; metadata logs | US ASCII DuckyScript subset, 32 KiB / 1024 steps. Manual hello works on Android; SD run needs acceptance. No reconnect autorun. |
| Airoha (newer pack) | Missing | Not integrated | Separate supplied code, outside this build. |

Research and provenance: [BLE protocol research](BLE_PROTOCOL_RESEARCH.md).

## Device, storage and build tools

| Area | Capabilities |
| --- | --- |
| Device | Brightness, device information, settings, team mode, clock/file dates and geofences. |
| SD/SPIFFS | SD File Browser, save/load lists, backup/restore SPIFFS and SD firmware update paths where configured. |
| Evidence | PCAP/log/GPX, Recon observation data, IoT CSV reports and BLE JSONL; Sessions indexes supported outputs. |
| Host tools | V8 compile script, clock synchronization, BLE smoke test, WiFi/BLE handoff test and Recon conversion utilities. |

## Hardware verification snapshot

The 2026-11-02 radio-handoff build compiled at 65% program storage and 30%
global RAM, flashed with hash verification, and retained **17 baseline APs,
21 after BLE, and 28 in WiFi Recon after BLE**. BLE restarted afterward. These
counts reflect the local radio environment, not a performance benchmark.

Still pending: complete touchscreen acceptance, wider GATT device interoperability,
and most active BLE module validation. Future work lives in the
[roadmap](ROADMAP.md), separately from this implementation inventory.
