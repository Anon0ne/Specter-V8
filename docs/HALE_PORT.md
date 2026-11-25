> Historical planning document. Status markers below predate the combined
> Specter Sessions + BLE build. Use [FEATURE_MAP.md](FEATURE_MAP.md) for the
> current inventory and [ROADMAP.md](ROADMAP.md) for future work.

# MarauderHale — HaleHound-inspired feature port (V8-radio scope)

**Base:** ESP32 Marauder v1.18.0 (MIT) · **Inspiration:** HaleHound-CYD v4.0 (behavior only — see provenance below)
**Branch model:** `master` tracks upstream · `hale/main` = integration · `hale/<feature>` per feature.

---

## Scope rule: Marauder V8's own radios only

We build on the **ESP32's built-in WiFi + BLE (2.4 GHz)** — the radios the Marauder V8 already has.
**No external radio hardware.** The following HaleHound features are therefore **out of scope**:

| Excluded | Why |
|----------|-----|
| All SubGHz / CC1101 (replay, brute force, Tesla, .sub, SubGHz spectrum) | needs external CC1101 |
| All 2.4 GHz / NRF24 (scanner, spectrum, Goodspeed sniffer, MouseJack) | needs external NRF24 |
| All NFC / RFID / PN532 (card suite, NTAG tools, Chameleon Ultra) | needs external PN532 — **confirmed out** |
| Jam Detect: SubGHz Sentinel, 2.4 GHz Watchdog, Full Spectrum | need CC1101 / NRF24 |

Everything we build must compile and run on a stock Marauder V8.

---

## Provenance / clean-room rule (do not skip)

HaleHound-CYD is **proprietary, binaries-only**; its license forbids reverse-engineering and disassembly.
- ✅ Use HaleHound's **README** (module descriptions, UX) as a behavioral spec.
- ✅ Use **ESP32-DIV (CiferTech, MIT)** and other open projects / public CVE research as code sources.
- ❌ Never disassemble HaleHound binaries or copy from them.
- Every new module header cites its idea/code source. Keeps the fork MIT-clean.

---

## Feature roadmap (all on V8 WiFi + BLE)

Status: ⬜ todo · 🟦 in progress · ✅ done. Effort: S/M/L.

**Build order (per user): WiFi first, BLE after.** Fill missing WiFi utilities and make existing
ones more intuitive, then do the BLE suite:
`Phase 1 (gate) → Phase 1b (Deauth Guardian) → Phase 3 (WiFi UX parity, incl. tap-to-deauth/clone)
→ Phase 4 (WiFi SIGINT extras) → **then** Phase 2 (BLE suite) → Phase 5 (drone)`.

### Phase 0 — Fork + scaffold  🟦
- ⬜ Fresh repo + remotes (`upstream`), `hale/main` branch — **done locally; GitHub push pending visibility choice**
- ⬜ `hale_features.h` feature-flag header (all off by default)
- ⬜ `THIRD_PARTY_NOTICE.md` provenance file
- ⬜ This roadmap

### Phase 1 — VALHALLA safety gate + Blue Team mode  (S)
Mimics HaleHound's liability gate. Offensive modules locked until the disclaimer is accepted; declining enters **Blue Team mode** (passive/defensive only), persisted across reboots (NVS/SPIFFS). Home-screen mode banner. *Do this first — it gates everything offensive.*

### Phase 1b — ⭐ Deauth Guardian (Blue Team)  (S–M)  ← user priority
Blue-Team deauth automation on the V8's built-in WiFi. Runs in Blue Team mode (detection is passive).
- ✅ **Detect (core)** — `DeauthGuardian` analyzer (`esp32_marauder/DeauthGuardian.{h,cpp}`): sliding-window rate, attacker→victim attribution, per-channel tally, SECURE/SUSPECT/UNDER-ATTACK verdict with cooldown hysteresis, edge-triggered alert events. Dependency-light + host-unit-tested (18/18). *Not yet wired into the firmware build.*
- ⬜ **Hunt (automation)** — auto channel-hop across 2.4 GHz to locate active attacks; identify attacker source MAC + victim BSSID/STA; live on-screen list. (S–M)
- ⬜ **Log** — timestamped events to SD, GPS-tagged (reuse Marauder GPS/wardrive) → a "deauth map." (S)
- ⬜ **Alert** — screen banner + LED/buzzer on detection. (S)
- ⬜ **Self-test (optional, VALHALLA-gated)** — fire a controlled deauth at *your own* AP and confirm Guardian detects it, closing the blue-team validation loop. Transmits → offensive-gated. (S)

> This supersedes the "WiFi Guardian" line under Phase 4; detection work lands here first.

### Phase 2 — BLE recon & exploit suite  (L)  — ⏸ deferred until the WiFi track is done
Built entirely on ESP32 built-in BLE. Implement from README behavior + public CVE PoCs.
- ⬜ **BLE Predator** — SCAN (discover + classify) → RECON (GATT enumerate) → HONEYPOT (clone as connectable server, capture writes). Loot → SD. (M)
- ⬜ **SkeletonKey** — connect, enumerate GATT, fire commands / fuzz writable characteristics; chains from Predator recon. (M)
- ⬜ **WhisperPair** — CVE-2025-36911 Google Fast Pair probe. (S–M, from public CVE)
- ⬜ **Airoha RACE** — CVE-2025-20700/1/2 unauthenticated GATT link-key extraction (Airoha chipsets). (M, from public CVE)
- ⬜ **KARR** — SouthWest Dealer Services BLE alarm detect + assess (read-only verdict). (S)
- ⬜ **Lunatic Fringe additions** — AirTag Replay (clone identity), AirTag Scream (force sound). Marauder already has FindMy sniff/spoof/sound. (S–M)
- ⬜ **Skimmer Hunter (BETA)** — BLE card-skimmer scoring + active confirmation pass. (M)

### Phase 3 — WiFi UX parity  (M)
Marauder already matches ~90% of HaleHound WiFi; fill the gaps.
- ⬜ **⭐ Scan AP → Tap-to-Deauth / Tap-to-Clone** (user priority) — close the gap where a selected AP has no clear path into a deauth/clone action. Three parts:
  - *Firmware (touchscreen):* make AP scan-list rows tappable → inline action menu (Deauth · Clone→Evil Twin · Info). Reuse existing `WIFI_ATTACK_DEAUTH_TARGETED` + Evil Portal; just add the selection→action bridge.
  - *Serial protocol:* add commands so a selected AP (by index/BSSID) can be deauthed or cloned from the host — e.g. `select -a <i>` → `attack -t deauth` already exist; add a one-shot `deauth -a <i>` / `clone -a <i>` convenience + status replies the GUI can parse.
  - *GUI software:* in the MarauderHale GUI (separate repo — `~/MarauderV8GUI-github` / MarauderGUIPro), add a Deauth/Clone button per AP row that sends those commands. **Cross-repo work — plan as its own task.**
- ⬜ **Probe Sniffer → auto Evil Twin** — auto-spawn portal with a probed SSID. (M)
- ⬜ **GARMR-style captive portal** polish on existing Evil Portal. (S)
- ⬜ **Auth Flood** variant + activity visualizer. (S)

### Phase 4 — SIGINT extras  (M)
- ⬜ **IoT Recon** — extend Marauder's ARP/DNS/HTTP/port scans with default-cred brute (`/creds.txt`) + report. (M)
- ⬜ **Unified Loot browser** — categorized view over SD captures. (S–M)
- ⬜ **Jam Detect: WiFi Guardian** — folded into **Phase 1b (Deauth Guardian)** above. (done there)

### Phase 5 — Drone extras + polish  (S–M)
- ⬜ **RID Spoof** — broadcast ASTM F3411 Remote ID over WiFi + BLE. (S–M)
- ⬜ **Drone Deauth** — targeted deauth of a drone control link. (S)
- ⬜ Radio Test / About pages updated for armed-module list. (S)

---

## Build / test
```bash
# arduino-cli replicating the CI recipe (see tools / scratch build scripts):
#   core: esp32:esp32@3.3.4   libs: TFT_eSPI, XPT2046, lv_arduino, JPEGDecoder,
#   NimBLE-Arduino 2.3.8, Adafruit_NeoPixel, ArduinoJson 6.18.2, LinkedList,
#   EspSoftwareSerial, ESP32Ping, MicroNMEA, Adafruit_BusIO, Adafruit_MAX1704X,
#   ESP32Async/AsyncTCP 3.4.8, ESP32Async/ESPAsyncWebServer 3.8.1
#   platform.txt patch: -Wl,-zmuldefs + -fno-exceptions (required for 3.3.4)
# Verified compiles: MARAUDER_V7 (esp32 d32) and MARAUDER_V8 (esp32c5).
```

### ⚠️ V8 hardware / display config is PRIVATE
- Marauder **V8 = ESP32-C5, 16 MB flash** (confirmed via esptool: ESP32-C5 rev v1.0, 16MB).
- `installer/targets.json` lists `MARAUDER_V8` under **`privateBuildFlags`**: the public
  source sets the driver (`HAS_ILI9341`, 240x320) but **omits the TFT pins/backlight**, so a
  public-source build falls back to default (original-ESP32) pins → **dead backlight / dark screen**.
- A **display-correct V8 binary needs the private config** (TFT CS/DC/RST/BL/SPI/TOUCH pins for the
  C5, correct `FlashSize=16M` + partition). Options: build on the owner's working V8 pipeline, or
  obtain the V8 `User_Setup`/pin mapping and add `User_Setup_marauder_v8.h` + `FlashSize=16M`.
- The Deauth Guardian **source** is board-agnostic and already compiles for V8/C5; only the
  display/flash packaging is blocked on the private config.

## Checklist discipline
Each feature: branch `hale/<feature>` → implement behind its `hale_features.h` flag → build on V8 → gate behind VALHALLA → update this file → PR into `hale/main`.
