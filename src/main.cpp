#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "mesh.h"
#include "pins.h"
#include "portal.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace {

constexpr uint32_t kBootScreenMs = 1500;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kDisplayHoldMs = 3000;
constexpr uint32_t kModeCommitMs = 3000;
constexpr uint32_t kDisplayRefreshMs = 500;

RepeaterConfig cfg;
String apSsid;

bool modeSelecting = false;
RepeaterMode selectedMode = RepeaterMode::WithWifi;
uint32_t modeSelectMs = 0;
uint32_t lastDisplayMs = 0;

String makeApSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "UAV-BOS-Repeater-%02X%02X", mac[4], mac[5]);
  return String(buf);
}

void enterRadioOnly() {
  portal::stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[main] repeater, WiFi off");
}

void enterWithWifi() {
  WiFi.persistent(false);
  WiFi.setHostname("uav-bos-repeater");
  portal::startAp(cfg, apSsid);
  Serial.println("[main] repeater, config AP");
}

void applyMode() {
  Serial.printf("[main] mode %s\n", config::modeName(cfg.mode));
  display::setMode(cfg.mode);
  if (cfg.usesWifi()) enterWithWifi();
  else enterRadioOnly();
}

void switchMode(RepeaterMode mode) {
  if (mode == cfg.mode) return;
  cfg.mode = mode;
  config::saveMode(mode);
  applyMode();
  lastDisplayMs = 0;
}

void handleWebModeRequest() {
  static bool pending = false;
  static RepeaterMode mode;
  static uint32_t requestMs = 0;
  RepeaterMode req;
  if (portal::takeModeRequest(req)) {
    pending = true;
    mode = req;
    requestMs = millis();
  }
  if (pending && millis() - requestMs >= 500) {
    pending = false;
    modeSelecting = false;
    switchMode(mode);
  }
}

enum class Button { None, Short, HoldDisplay };

Button pollButton() {
  static bool lastRaw = false;
  static bool pressed = false;
  static bool holdFired = false;
  static uint32_t changedMs = 0;
  static uint32_t pressedMs = 0;

  bool raw = digitalRead(PIN_BUTTON) == LOW;
  uint32_t now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    changedMs = now;
  }
  if (now - changedMs < kDebounceMs) return Button::None;

  if (raw && !pressed) {
    pressed = true;
    holdFired = false;
    pressedMs = now;
  } else if (raw && pressed) {
    if (!holdFired && now - pressedMs >= kDisplayHoldMs) {
      holdFired = true;
      return Button::HoldDisplay;
    }
  } else if (!raw && pressed) {
    pressed = false;
    if (!holdFired) return Button::Short;
  }
  return Button::None;
}

void handleButton() {
  switch (pollButton()) {
  case Button::Short:
    display::setOn(true);
    selectedMode = config::nextMode(modeSelecting ? selectedMode : cfg.mode);
    modeSelecting = true;
    modeSelectMs = millis();
    lastDisplayMs = 0;
    break;
  case Button::HoldDisplay:
    modeSelecting = false;
    display::toggle();
    break;
  default:
    break;
  }

  if (modeSelecting && millis() - modeSelectMs >= kModeCommitMs) {
    modeSelecting = false;
    lastDisplayMs = 0;
    switchMode(selectedMode);
  }
}

void checkReboot() {
  if (!portal::rebootRequested()) return;
  display::showBoot("Neustart");
  delay(800);
  ESP.restart();
}

void updateDisplay() {
  uint32_t now = millis();
  if (lastDisplayMs && now - lastDisplayMs < kDisplayRefreshMs) return;
  lastDisplayMs = now;

  if (modeSelecting) {
    display::showModeSelect(selectedMode, cfg.mode, kModeCommitMs - min(kModeCommitMs, now - modeSelectMs));
    return;
  }
  int clients = cfg.usesWifi() ? portal::apClients() : -1;
  display::showStatus(mesh::stats(), clients);
}

} // namespace

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  display::begin();
  display::showBoot(FW_VERSION);

  config::load(cfg);
  mesh::begin(cfg.mesh, cfg.meshKey);
  mesh::setActive(true);

  WiFi.persistent(false);
  WiFi.setHostname("uav-bos-repeater");
  apSsid = makeApSsid();

  Serial.printf("\n[main] UAV-BOS repeater %s, AP name %s\n", FW_VERSION, apSsid.c_str());
  delay(kBootScreenMs);
  applyMode();
}

void loop() {
  handleButton();
  handleWebModeRequest();
  if (cfg.usesWifi()) {
    portal::loop();
    checkReboot();
  }
  updateDisplay();
  delay(2);
}
