#pragma once
#include "configs.h"
#include <stdint.h>
namespace marauder {
enum class AutomationPanel : uint8_t { PROBE, IOT, CLOCK };
void showAutomationPanel(AutomationPanel panel);
bool automationTouchActive();
bool automationTouchLoop(uint32_t now);
}
