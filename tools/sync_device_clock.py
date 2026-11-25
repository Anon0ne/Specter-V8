#!/usr/bin/env python3
"""Sync the ESP32 clock; optionally apply an explicitly known date to one SD file."""
import argparse
import datetime as dt
import time


def commands(epoch, path=None, date=None):
    result = [f"clock set {epoch}"]
    if path is not None:
        if not date:
            raise ValueError("--file requires --date with an explicit timezone")
        known = dt.datetime.fromisoformat(date.replace("Z", "+00:00"))
        if known.tzinfo is None:
            raise ValueError("--date must include Z or a timezone offset")
        file_epoch = int(known.timestamp())
        if not 1577836800 <= file_epoch <= 4102444799:
            raise ValueError("--date must be in 2020..2099")
        if not path.startswith("/") or ".." in path or "\\" in path or "\n" in path or "\r" in path:
            raise ValueError("--file must be an absolute SD path without parent traversal")
        result.append(f"filedate {file_epoch} {path}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--file", help="Existing file's absolute SD path; content is unchanged")
    parser.add_argument("--date", help="Known file date, e.g. 2026-11-01T18:00:00-05:00")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        pending = commands(int(time.time()), args.file, args.date)
    except ValueError as exc:
        parser.error(str(exc))
    if args.dry_run:
        print("\n".join(pending))
        return
    try:
        import serial
    except ImportError:
        parser.error("pyserial is required (use the GUI's Python environment)")
    with serial.Serial(port=None, baudrate=args.baud, timeout=0.25) as device:
        device.dtr = False
        device.rts = False
        device.port = args.port
        device.open()
        device.reset_input_buffer()
        for command in pending:
            device.write((command + "\n").encode("utf-8"))
            device.flush()
            prefix = "[clock] synced" if command.startswith("clock set") else "[filedate] updated"
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                line = device.readline().decode("utf-8", errors="replace").strip()
                if line:
                    print(line)
                if line.startswith(prefix):
                    break
                if "ERROR" in line or line.startswith("Usage:"):
                    raise SystemExit("Device rejected command; no successful acknowledgement")
            else:
                raise SystemExit("No acknowledgement; check port, connection, and installed firmware")


if __name__ == "__main__":
    main()
