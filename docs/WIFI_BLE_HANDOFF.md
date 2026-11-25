# WiFi AP/STA and Recon after BLE

## Reproduced failure

On the V8, a fresh boot followed by a 12-second `scanall` retained 18 APs.
After a three-second BT+ Tracker scan, another `scanall` received beacon
frames but retained zero APs. WiFi Recon uses the same AP/STA scanner.
The idle NimBLE host remained allocated after BLE completion, leaving the
memory-guarded AP-list insertion unable to retain results.

## Fix

- Release the BT+ host before AP/STA capture, display and WiFi initialization.
- Re-evaluate the existing memory guard before WiFi resource allocation, so its
  recovery threshold can observe the newly available heap.
- Release the host when leaving the BLE touchscreen modal.
- Stop module jobs before host teardown; preserve NimBLE scanner objects with
  `deinit(false)` so lazy BLE restart can reuse its callbacks safely.
- Report a teardown failure without falsely marking the host released.

The memory guard remains enabled with its existing thresholds.

## Validation

`python3 tests/test_ble_radio_release.py` exercises the production release
routine, including ordering, idempotence and failed teardown. Existing modal,
inspection and device-table regression checks also pass.

`python3 tools/apsta_handoff_test.py --port /dev/ttyUSB0` checks baseline AP
capture, BLE-to-AP/STA, BLE-to-WiFi-Recon, and BLE restart afterward. It requires
nearby APs, may reset the board when opening serial, and changes the screen
while running. It does not associate with APs or connect to BLE targets.

Hardware result (2026-11-02): the patched build passed all transitions,
retaining 17 baseline APs, 21 after BLE, and 28 in WiFi Recon after BLE.
A final BLE restart also completed. Host teardown reported free heap rising
from 62,456 to 93,916 bytes on the first handoff. Serial was closed after testing.
The user independently confirmed AP entries were appearing again.
