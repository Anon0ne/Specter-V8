#pragma once
#include <sdkconfig.h>
#if !defined(MARAUDER_V8) || !defined(CONFIG_IDF_TARGET_ESP32C5)
#error "Specter supports Marauder V8 / ESP32-C5 only. Use tools/build_v8_ble.sh."
#endif
