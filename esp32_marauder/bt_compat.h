// ============================================================================
// bt_compat.h — NimBLE-Arduino 1.x / 2.x compatibility shims
//
// The pack targets NimBLE-Arduino 2.x (Marauder V8 / ESP32-C5 / Arduino
// core 3.x). These shims also compile against 1.4.x so the same files drop
// into classic-ESP32 Marauder builds. Where 2.x renamed an API the shim
// picks the right call at compile time via NIMBLE_MAJOR_VERSION.
// ============================================================================
#pragma once
#include <NimBLEDevice.h>
// NimBLE-Arduino 2.x ships no NimBLEVersion.h; this build targets 2.3.8.
#ifndef NIMBLE_MAJOR_VERSION
#define NIMBLE_MAJOR_VERSION 2
#endif

// Characteristic property bits (host constants, identical in both versions)
#ifndef BLE_GATT_CHR_F_WRITE_NR
#define BLE_GATT_CHR_F_WRITE_NR 0x40
#endif

// 1.x: NimBLERemoteCharacteristic::getProperties() -> uint8_t
// 2.x: property helpers (canRead/canWrite/...)
static inline uint8_t bt_chr_props(NimBLERemoteCharacteristic* ch) {
#if defined(NIMBLE_MAJOR_VERSION) && (NIMBLE_MAJOR_VERSION >= 2)
    return (ch->canRead()       ? BLE_GATT_CHR_F_READ     : 0) |
           (ch->canWrite()      ? BLE_GATT_CHR_F_WRITE    : 0) |
           (ch->canWriteNoResponse() ? BLE_GATT_CHR_F_WRITE_NR : 0) |
           (ch->canNotify()     ? BLE_GATT_CHR_F_NOTIFY   : 0) |
           (ch->canIndicate()   ? BLE_GATT_CHR_F_INDICATE : 0);
#else
    return ch->getProperties();
#endif
}

// 2.x removed NimBLEDevice::createScan(); getScan() exists in both.
static inline NimBLEScan* bt_get_scan() {
#if defined(NIMBLE_MAJOR_VERSION) && (NIMBLE_MAJOR_VERSION >= 2)
    return NimBLEDevice::getScan();
#else
    return NimBLEDevice::getScan();   // 1.4 also provides getScan()
#endif
}

// 1.x readValue() -> std::string ; 2.x -> NimBLEAttValue. Both expose
// .data()/.size(), so call sites use auto and never care.

// Scan-table addresses come from NimBLEAddress::getVal(), already in native order.
// The uint8_t-array constructor reverses bytes in NimBLE-Arduino 2.3.8.
static inline NimBLEAddress bt_address_from_native(const uint8_t* mac, uint8_t type) {
    ble_addr_t peer{};
    peer.type = type;
    memcpy(peer.val, mac, sizeof(peer.val));
    return NimBLEAddress(peer);
}
