#!/usr/bin/env python3
"""Verify the tester kit and flash its V8/C5 images using the recorded offsets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
LAYOUT = [("0x2000", "bootloader.bin"), ("0x8000", "partitions.bin"),
          ("0xe000", "boot_app0.bin"), ("0x10000", "Specter-V8.bin")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Windows COM port or Linux serial device")
    parser.add_argument("--verify-only", action="store_true", help="Check kit hashes without accessing hardware")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "TEST_KIT.json").read_text())
    if manifest.get("target") != "Marauder V8 / ESP32-C5":
        raise SystemExit("Not a Marauder V8 tester-kit manifest")
    for name, expected in manifest["sha256"].items():
        path = (ROOT / name).resolve()
        if ROOT not in path.parents:
            raise SystemExit("Invalid manifest path")
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Checksum mismatch: {name}; extract a fresh tester kit")
    for _, name in LAYOUT:
        if f"firmware/{name}" not in manifest["sha256"]:
            raise SystemExit(f"Missing verified flash image: {name}")
    print("Tester-kit checksums verified.")
    if args.verify_only:
        return
    port = args.port or input("V8 serial port (for example COM5 or /dev/ttyUSB0): ").strip()
    if not port or any(ord(c) < 32 for c in port):
        raise SystemExit("A valid serial port is required")
    print("Marauder V8 / ESP32-C5 only. Close the GUI serial connection first.")
    if input("Type V8 to flash this device: ").strip() != "V8":
        raise SystemExit("Cancelled")
    command = [sys.executable, "-m", "esptool", "--chip", "esp32c5", "--port", port,
               "--baud", "460800", "write-flash", "--flash-mode", "dio", "--flash-size", "8MB"]
    for offset, name in LAYOUT:
        command.extend([offset, str(ROOT / "firmware" / name)])
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
