#!/usr/bin/env python3
"""Package committed Marauder V8 source, excluding old firmware and other-board installers."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {"README.md", "LICENSE", "THIRD_PARTY_NOTICE.md", "CONTRIBUTING.md", "SECURITY.md",
              "User_Setup_marauder_v8.h", "platformio.ini", "PUBLIC_SOURCE_ORIGIN.json"}
TOOLS = {"build_v8_ble.sh", "build_v8_automation.sh", "setup_v8_dependencies.py", "v8_dependencies.json",
         "package_v8_source.py", "package_v8_testkit.py", "flash_v8.py",
         "sync_device_clock.py", "native_coverage.py"}


def included(name):
    p = PurePosixPath(name)
    if p.name.endswith((".bin", ".elf", ".map", ".pyc")):
        return False
    if len(p.parts) == 1:
        return name in ROOT_FILES
    if p.parts[0] == "esp32_marauder":
        return ((len(p.parts) == 2 and p.suffix in {".cpp", ".h", ".ino", ".csv"})
                or p.parts[1] == "data")
    if p.parts[0] == "tools":
        return p.name in TOOLS
    return p.parts[0] in {"docs", "test", "tests", "examples"} or name.startswith("pictures/specter/")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.version):
        parser.error("version must be a filename-safe release identifier")

    def git(*cmd):
        return subprocess.check_output(["git", "-C", str(ROOT), *cmd])

    sha = git("rev-parse", "--verify", args.ref + "^{commit}").decode().strip()
    names = git("ls-tree", "-r", "--name-only", sha).decode().splitlines()
    names = sorted(n for n in names if included(n))
    required = {"esp32_marauder/V8ReleaseTarget.h", "esp32_marauder/esp32_marauder.ino",
                "tools/build_v8_ble.sh", "tools/setup_v8_dependencies.py", "tools/v8_dependencies.json"}
    if not required.issubset(names):
        raise SystemExit("The selected commit is missing required V8 release/build files")
    args.output.mkdir(parents=True, exist_ok=True)
    stem = f"Specter-V8-{args.version}-source"
    path = args.output / (stem + ".zip")
    manifest = {"version": args.version, "commit": sha, "target": "Marauder V8 / ESP32-C5 only",
                "fqbn": "esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled",
                "source_only": True, "files": names}
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in names:
            info = zipfile.ZipInfo(f"{stem}/{name}", date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, git("show", f"{sha}:{name}"))
        archive.writestr(f"{stem}/SOURCE_RELEASE.json", json.dumps(manifest, indent=2) + "\n")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    (args.output / "SHA256SUMS.txt").write_text(f"{digest}  {path.name}\n")
    print(f"{path} ({path.stat().st_size} bytes; commit {sha})")


if __name__ == "__main__":
    main()
