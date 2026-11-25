# BLE touchscreen

Specter BLE launchers now open a modal result screen, refreshed every 250 ms
from `bt_ui_get()`. Normal menu rendering and touch dispatch pause while the
modal is open; BLE module ticks continue in the main loop.

- Stop closes the current BLE jobs and retains the last observed result snapshot.
- Back stops BLE jobs, reinitializes the TFT, restores portrait calibration and
  redraws the Specter BLE menu.
- Natural completion retains the last observed results, labelled as such rather
  than claiming an exact final snapshot. Current module getters return false at idle.
- Blocked, authorization and Stop menu messages use the same navigable screen.
- The opening touch must release before controls respond. A press dragged outside
  its original button does not activate that button.

`python3 tests/test_ble_touch.py` exercises the actual modal source with mocked
TFT/touch/module APIs under AddressSanitizer and UndefinedBehaviorSanitizer.
Leak detection is disabled for compatibility with sandbox tracing.

Hardware acceptance: open each passive scan, verify changing counters, Stop,
Back, repeat entry, natural completion, and navigation to another menu. Verify
no text/scroll corruption and no accidental selection after releasing Back.
Radio transitions involving legacy BLE or WiFi still require device validation.

## Validation — 2026-11-02

- Host touch lifecycle checks passed with ASan/UBSan.
- ESP32-C5 V8 compile passed: 2,186,722 bytes program storage (65%),
  101,124 bytes globals (30%).
- Flashed separate bootloader/partition/application images; upload hashes verified.
- Serial smoke test passed: boot, disarmed state, five-second BLE discovery
  (six advertisement frames), SD log, scan completion and stopped/idle state.
- Serial connection closed after testing. Physical touch/display acceptance is
  pending the user's device check; serial discovery does not verify visual layout.

## Device selection and GATT inspection

From **Bluetooth → Specter BLE → Predator Scan**, tap **Devices / Inspect**.
This stops the scan and opens four devices per page. Select a device to see
its full address, name, signal and advertised connectability. **Inspect GATT**
uses the existing recon path and preserves its Red Team and BT-arm gates.

Results show one characteristic per page: service and characteristic UUIDs,
handle, property mask, and a bounded hexadecimal read-value preview. The
legacy UUID byte strings are converted from little-endian storage for display.
No characteristic writes are added. Empty values do not distinguish a zero-length
value from an unreadable value or read failure.

Back returns through the device list to the scan result screen, and then the
existing Back returns to the menu. Selection checks the address and address
type again before connecting, rather than trusting a reused table index.

Recon now clears old results before each attempt, closes its log after a
connection failure, uses the installed NimBLE 2.x timeout units, and emits
JSON-compatible decimal property masks.

The current GATT recon call is synchronous: a connecting/wait message is drawn
before it starts, but touch cancellation is unavailable during connection,
discovery and reads. Connection timeout does not bound the entire discovery.

Validation: `python3 tests/test_ble_inspect.py` also runs the original modal
lifecycle checks. It covers page bounds, empty results, target changes,
connectability, authorization, failed/successful inspection, UUID formatting,
opening-touch suppression and clean return to the scan screen. These are host
checks with simulated devices; they do not establish live GATT interoperability.

Device-table insertion now initializes RSSI below normal measurements and
preserves table capacity when evicting an old slot. The production routine is
covered by `python3 tests/test_ble_device_table.py`.

Published protocol references and remaining evidence gaps are recorded in
[BLE_PROTOCOL_RESEARCH.md](BLE_PROTOCOL_RESEARCH.md).

### Inspection build validation — 2026-11-02

- Host modal, inspection and production device-table checks passed with ASan/UBSan.
- V8/C5 compile passed: 2,191,844 bytes program storage (65%); 101,500 bytes globals (30%).
- Separate-image upload completed with hash verification.
- Device boot and five-second discovery passed: five advertisement frames, SD
  log `/btloot/tracker/tracker_4.jsonl`, closed log and idle/disarmed state.
- Serial closed afterward. No automatic GATT connections were attempted;
  physical touchscreen acceptance and owned-target GATT interoperability remain pending.
- Application SHA-256: `750f73f5d9e46e05b124bbb227521fefee31dcdf606c81d0ce18a7e2d6ceecde`.

### GATT connection correction — 2026-11-02

The scanner stores `NimBLEAddress::getVal()` bytes in native NimBLE order.
The pointer/array address constructor in NimBLE-Arduino 2.3.8 reverses those
bytes, so using it for the stored scan address targeted the wrong address.
Predator recon now constructs the peer through `ble_addr_t`, preserving both
native bytes and address type. A host regression test uses the installed SDK's
actual constructors and the production recon address expression.

The controlled test also encountered NimBLE error 2 with the corrected address.
NimBLE's synchronous connection path includes automatic MTU negotiation, which
can fail with EALREADY even after a link is established. Inspection now connects
without automatic MTU exchange; ordinary ATT sizes support service/characteristic
discovery and reads. The final controlled test succeeded on the user's awake,
USB-powered Chameleon Ultra: **4 services and 7 characteristics**. This establishes
interoperability with that device, not every advertising peripheral.

Failures now expose the connection error code or distinguish no services from
no characteristics on the handheld. Empty discovery no longer counts as a
successful inspection. This remains a synchronous inspector; touch cancellation
during discovery is still a separate follow-up.

Validation: address/MTU-path regression, modal/inspector, radio-release and
BadBLE compiler/runner checks passed. V8 build: 2,241,670 bytes program storage
(67%) and 114,156 bytes globals (34%). Upload hash verified. No characteristic
writes or Chameleon commands were issued by the test; characteristic values
were not printed in the test report. Prior team/arming settings were restored,
the client disconnected and serial closed afterward.
