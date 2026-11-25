#!/usr/bin/env python3
"""Assemble a matched V8 firmware and public Windows GUI tester kit."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("firmware-build", "gui-package", "firmware-source", "gui-source", "boot-app0", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("version", "firmware-commit", "gui-commit", "gui-run"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    # Require the exact executable checksum supplied by the successful Windows CI artifact.
    checksum = (args.gui_package / "SHA256SUMS.txt").read_text().split()[0].lower()
    if digest(args.gui_package / "MarauderV8-Control.exe") != checksum:
        raise SystemExit("GUI executable does not match the CI artifact checksum")
    for name in ("source-install.json", "portable-install.json"):
        result = json.loads((args.gui_package / "verification" / name).read_text())
        if result.get("result") != "pass" or not result.get("frozen"):
            raise SystemExit("GUI artifact is missing a passing frozen-application check")
    stage = args.output / f"Specter-V8-{args.version}-Windows-TestKit"
    if stage.exists():
        raise SystemExit("Staging directory exists; use a fresh output directory")
    (stage / "firmware").mkdir(parents=True)
    for source, target in [("esp32_marauder.ino.bin", "Specter-V8.bin"),
                           ("esp32_marauder.ino.bootloader.bin", "bootloader.bin"),
                           ("esp32_marauder.ino.partitions.bin", "partitions.bin")]:
        shutil.copy2(args.firmware_build / source, stage / "firmware" / target)
    shutil.copy2(args.boot_app0, stage / "firmware/boot_app0.bin")
    shutil.copytree(args.gui_package, stage / "Windows-GUI")
    (stage / "source").mkdir()
    for source in (args.firmware_source, args.gui_source):
        shutil.copy2(source, stage / "source" / source.name)
    shutil.copy2(ROOT / "tools/flash_v8.py", stage / "flash_v8.py")
    (stage / "Flash-V8.cmd").write_bytes(b'''@echo off\r
cd /d "%~dp0"\r
py -3 -m venv .flash-venv\r
if errorlevel 1 goto failed\r
.flash-venv\\Scripts\\python.exe -m pip install esptool==5.1.0\r
if errorlevel 1 goto failed\r
.flash-venv\\Scripts\\python.exe flash_v8.py\r
if errorlevel 1 goto failed\r
pause\r
exit /b 0\r
:failed\r
echo Flash failed. Python 3, internet access and a free V8 COM port are required.\r
pause\r
exit /b 1\r
''')
    (stage / "SD").mkdir()
    (stage / "SD/index.html").write_text(
        '<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Specter V8 connectivity test</title><h1>Specter V8 portal is working</h1>'
        '<p>This is a connectivity test page. No credentials are requested.</p>\n')
    (stage / "START-HERE.txt").write_text(f'''Specter V8 tester kit — {args.version}

TARGET: Marauder V8 / ESP32-C5 only, 8 MB partition layout with PSRAM.
The GUI officially supports only V8. Other Marauder/ESP serial devices might
partly work but are untested and unsupported; never flash V8 images to them.
This is a testing prerelease, not a claim that every feature is validated.
KNOWN ISSUE: a client may fail to receive an IP on a later portal start.
No DHCP restart repair is claimed. Reboot if needed and report the symptoms.

1. Extract the entire ZIP. Install Python 3 on Windows if it is not present.
2. Connect the V8 by USB. Close any GUI/serial monitor using its COM port.
3. Run Flash-V8.cmd. Enter the V8 COM port and confirm the V8 target.
   The helper installs esptool 5.1.0, checks kit hashes and lets esptool check
   the ESP32-C5 chip. It writes bootloader at 0x2000 (NOT classic ESP32 0x1000),
   partitions at 0x8000, boot_app0 at 0xe000 and app at 0x10000. No full erase
   is requested. Keep a backup if preserving existing settings is important.
4. Run Windows-GUI/MarauderV8-Control.exe for portable use, or run
   Windows-GUI/INSTALL_WINDOWS.cmd to install it. Python is not required for
   the GUI itself. Select the V8 COM port, baud 115200, and Connect.
5. Check device status and SD Refresh first. For a harmless portal page,
   copy SD/index.html to the SD card root, select it and your test SSID,
   start the portal, connect a phone to that SSID, and open http://172.0.0.1/.

Use Flash-V8.cmd for this kit's firmware. The GUI's online firmware downloader
still points at upstream Marauder; it is not the Specter tester-kit updater.
Use the supplied helper for the matched support-image layout. Source-run Linux GUI instructions are in
the included GUI source archive; the frozen executable is Windows x64 only.

Firmware commit: {args.firmware_commit}
Public GUI commit: {args.gui_commit}
GUI CI: https://github.com/Anon0ne/marauder-v8-gui/actions/runs/{args.gui_run}
The public GUI includes Specter pages. Licensed Pro add-ons are not bundled.
MITM and the proposed portal input policy are not included.

Firmware was built from the curated source release. Portal startup/restart and
page delivery were validated on the previous firmware code before release
packaging/target guards; this exact release image still needs tester hardware
acceptance. Windows CI verified source/portable installs and frozen GUI pages,
but not this combined kit against a physical Windows USB device.

Report the two commits, Windows version, COM port/driver, steps, expected and
actual results, and console error lines. Do not attach private submitted data.

Linux flash: create a Python virtual environment, install esptool==5.1.0,
then run its Python with flash_v8.py --port /dev/ttyUSB0.
''')
    manifest = {"version": args.version, "target": "Marauder V8 / ESP32-C5",
                "firmware_commit": args.firmware_commit, "gui_commit": args.gui_commit,
                "gui_ci_run": args.gui_run,
                "sha256": {str(p.relative_to(stage)).replace("\\", "/"): digest(p)
                           for p in sorted(stage.rglob("*")) if p.is_file()}}
    (stage / "TEST_KIT.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = args.output / (stage.name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                z.write(path, str(path.relative_to(stage.parent)))
    print(archive)


if __name__ == "__main__":
    main()
