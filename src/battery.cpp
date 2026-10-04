#include "battery.h"

#include <WiFi.h>

#include "pins.h"

namespace battery {

namespace {

constexpr float kDivider = 3.2f; // (220k + 100k) / 100k
constexpr int kMinMv = 3000;
constexpr int kMaxMv = 4500;

int cellMillivolts(uint8_t pin) {
  analogSetPinAttenuation(pin, ADC_11db);
  (void)analogReadMilliVolts(pin);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(pin);
  return (int)((sum / 8) * kDivider);
}

// A floating ADC can sit inside the battery window. Two close samples reject that.
int stableCell(uint8_t pin) {
  int a = cellMillivolts(pin);
  int b = cellMillivolts(pin);
  if (a < kMinMv || a > kMaxMv || b < kMinMv || b > kMaxMv) return -1;
  if (a > b ? a - b > 80 : b - a > 80) return -1;
  return (a + b) / 2;
}

int readCell() {
  // V2.1 is the common board. Its pin wins when both look like a battery,
  // so a floating GPIO 13 does not hide the real reading.
  int v21 = stableCell(PIN_BATTERY_V21);
  int v2 = stableCell(PIN_BATTERY_V2);
  if (v21 >= 0) return v21;
  return v2;
}

int percentFromMv(int mv) {
  constexpr struct {
    int mv;
    int pct;
  } curve[] = {
      {4200, 100}, {4060, 90}, {3980, 80}, {3920, 70}, {3870, 60}, {3820, 50},
      {3790, 40},  {3770, 30}, {3740, 20}, {3680, 10}, {3300, 0},
  };
  constexpr int n = (int)(sizeof(curve) / sizeof(curve[0]));
  if (mv >= curve[0].mv) return 100;
  if (mv <= curve[n - 1].mv) return 0;
  for (int i = 1; i < n; i++) {
    if (mv < curve[i].mv) continue;
    int span = curve[i - 1].mv - curve[i].mv;
    int rise = curve[i - 1].pct - curve[i].pct;
    return curve[i].pct + (mv - curve[i].mv) * rise / span;
  }
  return 0;
}

} // namespace

int percent() {
  if (WiFi.getMode() != WIFI_OFF) return -1;

  int mv = readCell();
  static int filtered = -1;
  if (mv < 0) {
    filtered = -1;
    return -1;
  }
  if (filtered < 0) filtered = mv;
  else filtered = (filtered * 4 + mv) / 5;

  static int logged = -1;
  int pct = percentFromMv(filtered);
  if (pct != logged) {
    logged = pct;
    Serial.printf("[battery] %d mV, %d%%\n", filtered, pct);
  }
  return pct;
}

} // namespace battery
