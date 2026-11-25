"""Verify that release target guards reject unsupported hardware and packaging stays source-only."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_v8", ROOT / "tools/package_v8_source.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class V8ReleaseTests(unittest.TestCase):
    def test_both_build_helpers_find_cli_on_path(self):
        for script in ("build_v8_ble.sh", "build_v8_automation.sh"):
            with self.subTest(script=script), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "bin").mkdir()
                cli = root / "bin/arduino-cli"
                cli.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$CLI_ARGUMENTS"\n')
                cli.chmod(0o755)
                tft = root / "tft"
                tft.mkdir()
                (tft / "User_Setup_Select.h").write_text('#include <User_Setup.h>\n')
                env = {**os.environ, "HOME": tmp, "PATH": str(root / "bin") + ":/usr/bin:/bin",
                       "CLI_ARGUMENTS": str(root / "args"), "TFT_ESPI_DIR": str(tft)}
                env.pop("ARDUINO_CLI", None)
                subprocess.run(["bash", str(ROOT / "tools" / script), str(root / "out")],
                               env=env, check=True, capture_output=True)
                args = (root / "args").read_text()
                self.assertIn("esp32:esp32:esp32c5:", args)
                self.assertIn("CONFIG_ASYNC_TCP_STACK_SIZE=8192", args)
                self.assertIn("compiler.c.elf.extra_flags=", args)

    def test_target_guard(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "sdkconfig.h").write_text("")
            for defines, allowed in [([], False), (["MARAUDER_V8"], False),
                                     (["CONFIG_IDF_TARGET_ESP32C5"], False),
                                     (["MARAUDER_V8", "CONFIG_IDF_TARGET_ESP32C5"], True)]:
                run = subprocess.run(["g++", "-E", "-x", "c++", "-I" + tmp,
                                      *["-D" + d for d in defines],
                                      str(ROOT / "esp32_marauder/V8ReleaseTarget.h")],
                                     capture_output=True)
                self.assertEqual(run.returncode == 0, allowed)

    def test_archive_policy(self):
        for name in ["esp32_marauder/esp32_marauder.ino", "esp32_marauder/V8ReleaseTarget.h",
                     "User_Setup_marauder_v8.h", "tools/v8_dependencies.json"]:
            self.assertTrue(package.included(name), name)
        for name in ["User_Setup_cyd_2usb.h", "esp32_marauder/PreviousVersions/old.bin",
                     "esp32_marauder/libraries/TFT_eSPI/old.h", "FlashFiles/image.bin",
                     "C5_Py_Flasher_for_mini_v3/install.py", ".github/workflows/build_parallel.yml"]:
            self.assertFalse(package.included(name), name)

    def test_flash_helper_rejects_corrupt_kit_without_hardware(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shutil.copy2(ROOT / "tools/flash_v8.py", root / "flash_v8.py")
            (root / "firmware").mkdir()
            hashes = {}
            for name in ("bootloader.bin", "partitions.bin", "boot_app0.bin", "Specter-V8.bin"):
                path = root / "firmware" / name
                path.write_bytes(b"test fixture")
                hashes["firmware/" + name] = hashlib.sha256(path.read_bytes()).hexdigest()
            (root / "TEST_KIT.json").write_text(json.dumps(
                {"target": "Marauder V8 / ESP32-C5", "sha256": hashes}))
            cmd = [sys.executable, str(root / "flash_v8.py"), "--verify-only"]
            self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
            (root / "firmware/Specter-V8.bin").write_bytes(b"corrupted")
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Checksum mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
