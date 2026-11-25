#!/usr/bin/env bash
# Compile without flashing or modifying the shared Arduino libraries.
set -euo pipefail
repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-/tmp/marauder-v8-automation}
cli_bin=${ARDUINO_CLI:-arduino-cli}
if ! cli_bin=$(command -v "$cli_bin"); then
 if [[ -z ${ARDUINO_CLI:-} && -x $HOME/.local/bin/arduino-cli ]]; then
  cli_bin="$HOME/.local/bin/arduino-cli"
 else
  printf 'Arduino CLI not found. Add it to PATH or set ARDUINO_CLI.\n' >&2
  exit 127
 fi
fi
tft_source=${TFT_ESPI_DIR:-$HOME/Arduino/libraries/TFT_eSPI}
mkdir -p "$build_dir/libraries/TFT_eSPI" "$build_dir/bin"
tar -C "$tft_source" --exclude=.git -cf - . | tar -C "$build_dir/libraries/TFT_eSPI" -xf -
cp "$repo_dir/User_Setup_marauder_v8.h" "$build_dir/libraries/TFT_eSPI/User_Setup.h"
# Keep the selector's driver dispatch section (a bare include is insufficient).
sed -i 's|^[[:space:]]*#include <User_Setup[^>]*>|//#include <User_Setup.h>|' "$build_dir/libraries/TFT_eSPI/User_Setup_Select.h"
sed -i '0,/^\/\/#include <User_Setup.h>/s||#include <User_Setup.h>|' "$build_dir/libraries/TFT_eSPI/User_Setup_Select.h"
"$cli_bin" compile \
  --fqbn 'esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled' \
  --build-property "compiler.c.elf.extra_flags=" \
  --build-property 'compiler.cpp.extra_flags=-DMARAUDER_V8 -DCONFIG_ASYNC_TCP_STACK_SIZE=8192' \
  --build-property 'compiler.c.extra_flags=-DMARAUDER_V8 -DCONFIG_ASYNC_TCP_STACK_SIZE=8192' \
  --library "$build_dir/libraries/TFT_eSPI" \
  --build-path "$build_dir/build" --output-dir "$build_dir/bin" \
  --warnings none "$repo_dir/esp32_marauder"
