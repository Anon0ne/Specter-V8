// ============================================================================
// bt_predator.h — BLE Predator: 3-phase
//   SCAN      discover connectable devices + threat classify (passive)
//   RECON     connect, enumerate full GATT table, dump JSONL to SD (active)
//   HONEYPOT  re-advertise as the target and log every GATT WRITE (active)
// RECON and HONEYPOT are gated behind `bt arm`.
// ============================================================================
#pragma once
#include "bt_core.h"
#include <vector>

struct BtGattCharInfo {
    uint16_t handle;
    String   svcUuid;      // 16-bit hex or full 128-bit string
    String   chrUuid;
    uint8_t  props;
    String   valueHex;     // initial read, empty if not readable
};

void   bt_predator_register();          // adds scan sink (connectable tagging)
bool   bt_predator_command(const String& c);
void   bt_predator_loop();
void   bt_predator_stop();
bool   bt_predator_status(BtUiState& out);
std::vector<BtGattCharInfo>* bt_predator_recon_table();  // shared w/ skeleton
String bt_predator_target_name();
bool   bt_predator_recon_done();

String bt_predator_recon_error();
