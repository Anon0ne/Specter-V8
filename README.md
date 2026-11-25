<p align="center">
  <img src="pictures/specter/specter-mark.svg" alt="Specter ghost and radio-wave logo" width="240">
</p>

<h1 align="center">Specter</h1>
<p align="center"><strong>WiFi and BLE field assessment firmware for the Marauder V8.</strong></p>
<p align="center">
  <a href="docs/FEATURE_MAP.md">Feature map</a> ·
  <a href="docs/ROADMAP.md">Roadmap</a> ·
  <a href="#build-for-the-v8">Build</a> ·
  <a href="LICENSE">MIT license</a>
</p>

Specter builds on [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder)
with touchscreen workflows, organized evidence, WiFi automation, and a BLE
inspection suite. It uses the Marauder V8's onboard ESP32-C5 radios: dual-band
2.4/5 GHz WiFi and Bluetooth Low Energy.

**Marauder V8 only — public beta:** `main` is the consolidated firmware branch.
Only the Marauder V8 / ESP32-C5 is supported by the release build. Inherited
board-specific code remains for history, but the sketch rejects other targets.
The [feature map](docs/FEATURE_MAP.md) distinguishes implemented interfaces from
hardware validation. MITM feasibility code and the unimplemented portal input
policy are not included.

**Supported hardware: Marauder V8 / ESP32-C5 only. Do not flash onto other
Marauder revisions or ESP boards.** The companion GUI officially supports only
the V8; some other Marauder/ESP hardware may partially work over serial but is
untested and unsupported. That possibility does not extend firmware support.

This is a testing prerelease with known limitations, including a reported
second-start portal DHCP failure. See [beta notes](docs/PUBLIC_BETA.md).

## What's new in Specter

| Area | Features |
| --- | --- |
| **Specter Sessions** | Named runs, session folders, output index, history, interrupted-run detection, and device health. |
| **WiFi workflows** | Deauth Guardian, AP tap-to-deauth/clone, exact-SSID Probe to Portal automation, and scoped IoT Recon with SD reports. |
| **BLE suite** | Tracker, Phantom, Beacon, Spoofer, Predator, SkeletonKey, WhisperPair, KARR, Skimmer and BadBLE keyboard; interface coverage varies. |
| **BadBLE DuckyScript** | Paged SD script selection, manual Run, live progress, Stop/Back, metadata event logs and a harmless built-in hello test. |
| **BLE touchscreen** | Live module status, retained result snapshots, Stop/Back, a paged device picker, and read-only GATT inspection. |
| **SD and time** | SD File Browser, UTC clock synchronization from GPS or serial, and correction of known file modification dates. |
| **Team modes** | Persistent Blue/Red Team selection, offensive WiFi mode gates, and separate BT+ arming for active modules. |

### Sessions and evidence

Open **Specter → Sessions**, name the run, and start it before using the tools.
Supported captures, reports, Recon output and BLE logs are indexed beneath:

```text
/specter/sessions/<session-id>/
  session.json
  outputs.jsonl
  complete.json
  captures/
  reports/
  recon/
  bluetooth/
```

End the session explicitly to finalize it. A rebooted, unfinished run appears
as interrupted. Sessions organize output; they do not start scans or enable
packet capture automatically. Enable SavePCAP when captures are wanted.
See [Sessions](docs/SPECTER_SESSIONS.md).

### WiFi

Specter retains Marauder's scanning, sniffing, capture and assessment tools,
including:

- AP/station discovery, probe/beacon/deauth sniffing, EAPOL/PMKID capture, raw
  capture, packet monitoring, channel analysis and channel summaries.
- Ping, ARP and conventional TCP-port scanners; network joining, saved networks,
  AP/SSID/station lists, MAC controls, and GPS/wardrive functions where configured.
- Existing beacon/probe/authentication and deauthentication test modes, AP clone,
  Karma, Evil Portal, and other upstream WiFi assessment modes.
- Remote ID scanning and device views, plus Pwnagotchi, Pineapple and MultiSSID
  detection workflows. Detection results depend on observable traffic.

Specter additions:

- **Deauth Guardian:** rate/window-based deauthentication detection and alert state.
- **Tap Deauth/Clone:** choose a discovered AP and open its action menu.
- **Probe to Portal:** listen for a directed probe matching one exact SSID, then
  transition to the configured existing portal; listening expires after 60 seconds.
- **IoT Recon:** scan a selected local subnet range for conventional TCP ports and
  save a report. Port numbers suggest services; they do not prove a device identity.
- **Recon missions:** WiFi/BLE dashboards and observation logs, integrated with Sessions.

GUI/storage integration: [SD protocol](docs/GUI_SD_COMPATIBILITY.md) ·
[Portal exercise](docs/PORTAL_EXERCISE.md) · [AP selection and Stop](docs/AP_CLONE_AND_STOP.md).

Details: [WiFi automation and file dates](docs/WIFI_AUTOMATION.md) ·
[BLE-to-WiFi handoff fix](docs/WIFI_BLE_HANDOFF.md).

### Bluetooth Low Energy

Legacy Marauder BLE functions remain alongside **Bluetooth → Specter BLE**.
The suite contains nine original assessment modules plus BadBLE keyboard, with the complete interface breakdown in
the [BLE feature map](docs/FEATURE_MAP.md#specter-ble-suite).

For GATT inspection:

1. Open **Bluetooth → Specter BLE → Predator Scan**.
2. Tap **Devices / Inspect** to stop the scan and browse discovered devices.
3. Select your device and tap **Inspect GATT**. The existing Red Team and BT-arm
   requirements apply.
4. Page through characteristic UUIDs, handles, properties and read-value previews.
5. Use Back to return through the device list, scan screen and menu.

The current recon call is synchronous: the screen shows a wait message, but
connection/discovery cannot yet be cancelled by touch. The inspector adds no
characteristic writes. See [BLE touchscreen](docs/BLE_TOUCHSCREEN.md).

### BadBLE keyboard and SD scripts

Open **Bluetooth → Specter BLE → BadBLE Keyboard**, pair **Specter Keyboard**
on your own test host and wait for **Ready**. Use **Scripts** to select a file
from `/btloot/payloads/`, then **Run** to execute it. Selection and pairing do
not type anything. Stop cancels and disconnects; Back restores the menu.

The supplied DuckyScript subset supports STRING/STRINGLN, DELAY, DEFAULTDELAY,
REPEAT, modifier combinations and named keys. `.txt`, `.duck` and `.ducky` files
are bounded to 32 KiB / 1024 steps and validated before execution. Nonblocking
execution keeps touch controls responsive; run metadata is written through the
BLE Session/SD logger. A harmless built-in hello is always available, and
`bb install-demo` adds the SD example without replacing an existing file.

[Setup, script grammar, commands and validation](docs/BADBLE_KEYBOARD_TEST.md) ·
[SD example](examples/badble/hello-specter.txt).
The manual hello test has user confirmation on Android. SD-script acceptance
on the paired host remains a separate hardware test.

### SD files and correct dates

Use **Device → SD File Browser** for files and **Device → Clock / File Dates**
for clock/date tools. A valid GPS fix can synchronize UTC; without GPS, use the
host helper:

```sh
python3 tools/sync_device_clock.py --port /dev/ttyUSB0
```

After full power loss, the device needs a time source again. Unsynced time is
reported explicitly. Existing 1970 timestamps cannot reveal the original date;
apply a replacement only when the real date is known.

## Hardware and build

The verified target is the **Marauder V8 / ESP32-C5**, with the V8 TFT/touch
configuration. The board has 16 MB physical flash; the tested build currently
uses the **8 MB partition layout** with PSRAM enabled. GPS features require an
available module/fix, and SD features require a mounted card.

This integration does not add CC1101, nRF24 or NFC hardware. ESP32-C5 supports
BLE, not Bluetooth Classic. Other board configurations remain as inherited source; they are not supported
release targets.

### Build for the V8

The tested local toolchain uses Arduino-ESP32 **3.3.4**, NimBLE-Arduino **2.3.8**,
and the ESP32-C5-compatible **H4W9/TFT_eSPI** fork. For pinned dependency setup
and source-release instructions, see [V8 source release](docs/V8_SOURCE_RELEASE.md).
With the required libraries installed:

```sh
bash tools/build_v8_ble.sh ../specter-v8-build
```

The script stages a separate TFT library configuration, selects the V8 setup,
and builds without flashing or changing your shared TFT library. Arduino CLI
target:

```text
esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled
```

The earlier radio-handoff build used **65% program storage and 30% global RAM**;
new add-ons have separate build and validation results in their linked guides.
Flash images must match the V8 and their intended offsets. App-only and merged
images are different artifacts; preserve a known-good device backup and use
the matching upload procedure. [Build and flash validation](docs/BLE_FLASH_VALIDATION.md).

## Validation and current limits

- AP/STA discovery and WiFi Recon have been tested after BLE use; the handoff
  releases the idle BLE host so WiFi can retain AP entries again.
- The manual BadBLE keyboard hello was user-tested, including Android. SD DuckyScript
  parsing/execution has host-test coverage; paired-host SD execution needs acceptance.
- Host tests cover modal touch behavior, device selection, UUID formatting,
  device-table eviction and radio-release ordering.
- BLE discovery and SD logging have device smoke-test coverage. Read-only GATT
  inspection succeeded on a user-owned Chameleon Ultra (4 services, 7 characteristics)
  after address-order and MTU-path corrections. Other devices and most active BLE
  tools still need owned-target validation.
- Fast Pair example model IDs are placeholders; KARR signatures are not a
  validated vulnerability database; skimmer detection is heuristic and BLE-only.
- Airoha from the newer supplied pack is not integrated. Dedicated GARMR polish,
  the proposed authentication-flood visualizer and the other roadmap items are
  not advertised as completed features.
- Structured Guardian Session logging and several BLE target/action screens are
  still follow-ups. [Protocol research](docs/BLE_PROTOCOL_RESEARCH.md) ·
  [Roadmap](docs/ROADMAP.md).

## Credits and license

Maintained by [Anon0ne](https://github.com/Anon0ne). Specter is based on
**ESP32 Marauder by Just Call Me Koko**; the tested base reports Marauder v1.18.0
and Specter v1.0.0. Upstream credits and the [MIT license](LICENSE) are retained.
See [third-party notices](THIRD_PARTY_NOTICE.md) for project provenance and hardware references.

Use active assessment features only on devices and networks you own or are
explicitly authorized to test.

## Companion GUI and premium Heatmap

The Specter V8 public-beta GUI excludes Heatmap. Heatmap is available only with
premium membership purchased directly from [Anon0ne](https://github.com/Anon0ne).
Contact Anon0ne directly for membership details, pricing and premium-edition
access. Premium code and licensing infrastructure are not part of this firmware
or the public-beta GUI package.
