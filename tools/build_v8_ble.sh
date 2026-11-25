#!/usr/bin/env bash
# Compile the V8 BLE suite without changing shared libraries or flashing.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=${1:-/tmp/specter-ble-build}
cli=${ARDUINO_CLI:-arduino-cli}
if ! cli=$(command -v "$cli"); then
 if [[ -z ${ARDUINO_CLI:-} && -x $HOME/.local/bin/arduino-cli ]]; then
  cli="$HOME/.local/bin/arduino-cli"
 else
  printf 'Arduino CLI not found. Add it to PATH or set ARDUINO_CLI.\n' >&2
  exit 127
 fi
fi
tft=${TFT_ESPI_DIR:-$HOME/Arduino/libraries/TFT_eSPI}
mkdir -p "$out/libraries/TFT_eSPI" "$out/bin"
tar -C "$tft" --exclude=.git -cf - . | tar -C "$out/libraries/TFT_eSPI" -xf -
cp "$repo/User_Setup_marauder_v8.h" "$out/libraries/TFT_eSPI/User_Setup.h"
sed -i 's|^[[:space:]]*#include <User_Setup[^>]*>|//#include <User_Setup.h>|' "$out/libraries/TFT_eSPI/User_Setup_Select.h"
sed -i '0,/^\/\/#include <User_Setup.h>/s||#include <User_Setup.h>|' "$out/libraries/TFT_eSPI/User_Setup_Select.h"
flags='-DMARAUDER_V8 -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=2 -DCONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256 -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=1 -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=1 -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=1 -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=1'
# AsyncTCP's default 16 KiB stack no longer fits the V8's fragmented internal
# heap after AP startup. Apply this to the library compilation, not just headers.
flags+=' -DCONFIG_ASYNC_TCP_STACK_SIZE=8192'
if [[ ${SPECTER_HTTP_DIAGNOSTICS:-0} == 1 ]]; then
 flags+=' -DCORE_DEBUG_LEVEL=1 -DSPECTER_HTTP_DIAGNOSTICS=1'
fi
"$cli" compile --fqbn 'esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled' \
 --build-property "compiler.c.elf.extra_flags=" \
 --build-property "compiler.cpp.extra_flags=$flags" \
 --build-property "compiler.c.extra_flags=$flags" \
 --library "$out/libraries/TFT_eSPI" --build-path "$out/build" \
 --output-dir "$out/bin" --warnings none "$repo/esp32_marauder"
