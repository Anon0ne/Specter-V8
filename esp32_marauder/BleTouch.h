#pragma once
#include <stdint.h>
namespace specter {
void showBle(const char* command, const char* title);
void showBleMessage(const char* title, const char* message);
void resumeBleScreen();
bool bleTouchLoop(uint32_t now);
}
