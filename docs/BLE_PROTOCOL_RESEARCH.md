# BLE protocol research and remaining gaps

Checked 2026-11-02. Published protocol information is separate from observed
hardware behavior; a matching advertisement is not a vulnerability verdict.

## Fast Pair

Google publishes service UUID `0xFE2C`, the readable Model ID characteristic
`FE2C1233-8366-4814-8EB0-01DE32100BEA`, and its three-byte model-ID value.
Model IDs are assigned during registration. A protocol specification does not
supply an exhaustive, verified product/model catalogue; the existing example
IDs in the pack are still placeholders. Record an observed ID with provenance
rather than assigning an unverified product name.

Sources: [GATT characteristics](https://developers.google.com/nearby/fast-pair/specifications/characteristics),
[model registration](https://developers.google.com/nearby/fast-pair/specifications/service/modelregistration).

## Standard GATT inspection

Use the Bluetooth SIG Assigned Numbers for standard service/characteristic
labels. Vendor UUIDs remain vendor-specific unless independently documented.

Source: [Bluetooth SIG Assigned Numbers](https://www.bluetooth.com/specifications/assigned-numbers/).

## KARR / SWDS

UC San Diego's original study documents affected aftermarket systems and a
KARR firmware update released July 20, 2026. The project explicitly withholds
exploitation details. This research does not validate Specter's generic KARR
signature table or establish a reliable UUID-to-vulnerability mapping. Keep
matches labelled as candidates; patched and unpatched devices must not be
classified solely from name/advertisement resemblance.

Source: [BLE Theft Auto research project](https://bleauto.sysnet.ucsd.edu/).

## Airoha

The vendor advisory and original ERNW research document RACE-related
vulnerabilities and affected chip/SDK families. This supports an advisory and
inventory reference, not a finding that an individual headset is vulnerable.
The updated ZIP's Airoha implementation remains outside this integration.

Sources: [Airoha 2025 bulletin](https://www.airoha.com/product-security-bulletin/2025),
[ERNW White Paper 74](https://ernw.de/en/whitepapers/issue-74.html).

## Radio limitations and skimmer claims

The V8's ESP32-C5 supports BLE, not Bluetooth Classic. A BLE-only scan cannot
cover Classic-only skimmers. The pack's skimmer scores remain unvalidated
heuristics; the scanner should not imply that a negative result proves safety.

Sources: [Espressif C5 Bluetooth overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-guides/ble/overview.html),
[original Bluetooth skimmer-detection study](https://cseweb.ucsd.edu/~schulman/docs/sec19-bluetana.pdf).

## API correction made in this change

Installed NimBLE-Arduino 2.3.8 documents `setConnectTimeout()` in milliseconds
(`src/NimBLEClient.cpp`). Recon previously divided the configured 4000 ms by
1000, producing a 4 ms timeout. The 2.x call now receives milliseconds; the
legacy branch retains seconds. No dependency upgrade was made.

## Follow-ups requiring evidence

- Capture-backed product model IDs and confidence labels.
- Validated KARR detection signatures, without a vulnerability verdict from advertisements alone.
- Known-device validation of skimmer heuristics and explicit BLE-only coverage.
- Owned-device GATT inspection tests across success, rejection, disconnect and empty-service cases.
- Asynchronous/cancellable GATT discovery; current recon runs synchronously.
