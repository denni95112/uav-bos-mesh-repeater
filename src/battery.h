#pragma once

namespace battery {

// 0..100 while WiFi is off, or -1 when no battery is connected.
// GPIO 13 is ADC2, so this is not valid in the config-AP mode.
int percent();

} // namespace battery
