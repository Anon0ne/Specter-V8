#pragma once
#include <stdint.h>
namespace specter {
void showHub();
bool touchActive();
bool touchLoop(uint32_t now);
}
