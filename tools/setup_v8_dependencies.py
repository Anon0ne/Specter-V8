#!/usr/bin/env python3
"""Install pinned V8 dependencies into a separate Arduino user directory."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--cli", default=os.environ.get("ARDUINO_CLI", "arduino-cli"))
    args = parser.parse_args()
    cli = shutil.which(args.cli)
    if not cli:
        parser.error("Install arduino-cli and add it to PATH, or supply --cli")
    root = args.destination.resolve()
    (root / "libraries").mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "ARDUINO_DIRECTORIES_USER": str(root)}
    lock = json.loads(Path(__file__).with_name("v8_dependencies.json").read_text())

    def run(*cmd):
        subprocess.run(cmd, check=True, env=env)

    run(cli, "core", "update-index", "--additional-urls", lock["board_index"])
    run(cli, "core", "install", lock["core"], "--additional-urls", lock["board_index"])
    run(cli, "lib", "update-index")
    run(cli, "lib", "install", "--no-deps", *lock["libraries"])
    for lib in lock["git_libraries"]:
        path = root / "libraries" / lib["name"]
        if not path.exists():
            run("git", "clone", "--filter=blob:none", "--no-checkout", lib["url"], str(path))
            run("git", "-C", str(path), "checkout", "--detach", lib["commit"])
        revision = subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(
            ["git", "-C", str(path), "status", "--porcelain"], text=True).strip()
        if revision != lib["commit"] or dirty:
            raise SystemExit(f"{path} differs from the pinned checkout; use an empty destination")
    print(f"Dependencies ready in {root}")
    print(f"Build with ARDUINO_DIRECTORIES_USER={root} and TFT_ESPI_DIR={root / 'libraries/TFT_eSPI'}")


if __name__ == "__main__":
    main()
