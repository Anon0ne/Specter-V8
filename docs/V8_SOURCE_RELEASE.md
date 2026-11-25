# Marauder V8 source release

`main` is the consolidated Specter firmware branch. This release supports only
the Marauder V8 / ESP32-C5, V8 touchscreen setup, 8 MB partition layout and PSRAM.
Inherited board code remains in Git for history. The sketch rejects other targets,
and the source ZIP omits older firmware binaries and other-board installers.

## Build from the source ZIP (Linux or WSL)

Install Python 3, Git and Arduino CLI 1.3.1 or newer. Extract the ZIP, enter its
top directory, then run:

```sh
python3 tools/setup_v8_dependencies.py /tmp/specter-v8-deps
export ARDUINO_DIRECTORIES_USER=/tmp/specter-v8-deps
export TFT_ESPI_DIR=/tmp/specter-v8-deps/libraries/TFT_eSPI
bash tools/build_v8_ble.sh /tmp/specter-v8-build
```

Dependencies are pinned in `tools/v8_dependencies.json`. Library installation
uses a separate Arduino user directory; the ESP32 core uses Arduino CLI's data
directory. The setup and build scripts do not flash a device. Internet access is
required for dependency installation. The ZIP does not contain prebuilt firmware.

The fixed build applies `CONFIG_ASYNC_TCP_STACK_SIZE=8192` to the library as well
as the sketch. Do not omit this when using another build system: the 16 KB
default exceeded the contiguous internal-memory headroom on the tested V8.

## Included changes and validation

The release consolidates Sessions, SD file-manager support, BLE touchscreen and
GATT inspection, BadBLE lifecycle fixes, AP selection controls and the portal
HTTP listener/stack-memory fix. The user confirmed portal page delivery after
the memory fix; startup and stop/reconfigure/restart were checked over serial.
These are targeted checks, not a claim that every feature has hardware validation.
See `FEATURE_MAP.md` and the individual feature guides for remaining limits.

MITM is an isolated feasibility experiment and is not included. The proposed
portal email/password policy has not been implemented and is not included.
The older `hale/ble-suite` branch contains a superseded touchscreen launcher;
the consolidated branch uses BleTouch instead of reintroducing that launcher.
Legacy branches remain in the private development archive; the public repository
starts from this reviewed source snapshot. Use its `main` branch.

The GUI repositories already contain their tested integrations on `main`:
`Anon0ne/marauder-v8-gui` (public companion) and `Anon0ne/MarauderGUIPro`
(separate Pro application). They are not bundled into firmware source.

The separate Windows tester-kit asset includes built V8 images, a matching public
GUI from its successful Windows CI run, both source archives, exact revisions,
checksums, a C5-specific flash helper and a harmless SD connectivity page.
Read `START-HERE.txt` after extracting it. The current GUI's upstream downloader
and classic-ESP32 full-flash defaults are not the installer for this kit; use the
supplied `Flash-V8.cmd`. Licensed Pro add-ons are not included in the public kit.

## Release verification

`SOURCE_RELEASE.json` inside the archive identifies its exact Git commit and
V8 build target. Compare the ZIP's SHA-256 with the published `SHA256SUMS.txt`.
GitHub's automatic source archives contain the complete repository history tree;
use the named `Specter-V8-…-source.zip` asset for the curated V8 package.

The multi-board build/nightly/installer publishing workflows inherited from
upstream were replaced with a V8-only source/build workflow. No device is flashed
by that workflow, and no firmware binaries are published by it.
