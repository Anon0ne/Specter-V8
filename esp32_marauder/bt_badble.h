#pragma once
#include "bt_core.h"
bool bt_badble_command(const String& command);
void bt_badble_loop();
void bt_badble_stop();
bool bt_badble_status(BtUiState& out);
bool bt_badble_busy();
bool bt_badble_ready();
bool bt_badble_typing();
size_t bt_badble_scripts(String* names, size_t capacity, size_t offset, bool& more);
bool bt_badble_select(const String& name);
bool bt_badble_run_selected();
String bt_badble_selected();
String bt_badble_notice();

void bt_badble_host_released();
