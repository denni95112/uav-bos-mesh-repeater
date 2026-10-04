#include "config.h"

#include <Preferences.h>
#include <mbedtls/base64.h>

#ifndef MESH_PSK_B64
#define MESH_PSK_B64 ""
#endif

namespace config {

static const char *kNamespace = "repeater";

// A key stored on the device wins over the build key, so firmware updates never swap keys.
// Devices without a stored key adopt the build key once.
static String loadMeshKey() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  String key = prefs.getString("meshkey", "");
  prefs.end();
  if (validMeshKey(key)) return key;
  key = MESH_PSK_B64;
  if (!validMeshKey(key) || key == kPlaceholderMeshKey) return kPlaceholderMeshKey;
  prefs.begin(kNamespace, false);
  prefs.putString("meshkey", key);
  prefs.end();
  return key;
}

bool validMeshKey(const String &b64) {
  if (b64.isEmpty() || b64.length() > 64) return false;
  uint8_t buf[48];
  size_t olen = 0;
  if (mbedtls_base64_decode(buf, sizeof(buf), &olen, (const unsigned char *)b64.c_str(), b64.length()) != 0)
    return false;
  return olen == 16 || olen == 32;
}

void load(RepeaterConfig &cfg) {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  cfg.apPass = prefs.getString("appass", "");
  uint8_t mode = prefs.getUChar("mode", (uint8_t)RepeaterMode::WithWifi);
  MeshSettings m;
  m.preset = (LoraPreset)prefs.getUChar("preset", (uint8_t)m.preset);
  m.slot = prefs.getUChar("slot", m.slot);
  m.hopLimit = prefs.getUChar("hops", m.hopLimit);
  m.txPowerDbm = prefs.getChar("txpower", m.txPowerDbm);
  m.relayMode = (RelayMode)prefs.getUChar("relay", (uint8_t)m.relayMode);
  m.foreignAirtimePct = prefs.getUChar("fairtime", m.foreignAirtimePct);
  prefs.end();
  cfg.mesh = sanitize(m);
  cfg.meshKey = loadMeshKey();
  cfg.mode = mode <= (uint8_t)RepeaterMode::RadioOnly ? (RepeaterMode)mode : RepeaterMode::WithWifi;
}

void save(const RepeaterConfig &cfg) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putString("appass", cfg.apPass);
  prefs.putUChar("mode", (uint8_t)cfg.mode);
  MeshSettings m = sanitize(cfg.mesh);
  prefs.putUChar("preset", (uint8_t)m.preset);
  prefs.putUChar("slot", m.slot);
  prefs.putUChar("hops", m.hopLimit);
  prefs.putChar("txpower", m.txPowerDbm);
  prefs.putUChar("relay", (uint8_t)m.relayMode);
  prefs.putUChar("fairtime", m.foreignAirtimePct);
  if (validMeshKey(cfg.meshKey) && cfg.meshKey != kPlaceholderMeshKey) prefs.putString("meshkey", cfg.meshKey);
  prefs.end();
}

MeshSettings sanitize(MeshSettings s) {
  if ((uint8_t)s.preset >= (uint8_t)LoraPreset::Count) s.preset = LoraPreset::LongFast;
  if (s.slot > kMaxSlot) s.slot = 0;
  s.hopLimit = constrain(s.hopLimit, kMinHopLimit, kMaxHopLimit);
  s.txPowerDbm = constrain(s.txPowerDbm, kMinTxPowerDbm, kMaxTxPowerDbm);
  // SX1276 PA_BOOST is 2-17 dBm, or exactly 20 dBm. 18 and 19 are not configurable.
  if (s.txPowerDbm == 18 || s.txPowerDbm == 19) s.txPowerDbm = 17;
  if ((uint8_t)s.relayMode > (uint8_t)RelayMode::None) s.relayMode = RelayMode::All;
  if (s.foreignAirtimePct > kMaxForeignAirtimePct) s.foreignAirtimePct = kMaxForeignAirtimePct;
  return s;
}

void saveMode(RepeaterMode mode) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar("mode", (uint8_t)mode);
  prefs.end();
}

void clear() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.clear();
  prefs.end();
}

RepeaterMode nextMode(RepeaterMode mode) {
  return mode == RepeaterMode::WithWifi ? RepeaterMode::RadioOnly : RepeaterMode::WithWifi;
}

const char *modeName(RepeaterMode mode) {
  return mode == RepeaterMode::RadioOnly ? "Repeater ohne WLAN" : "Repeater + WLAN";
}

const char *modeShort(RepeaterMode mode) { return mode == RepeaterMode::RadioOnly ? "LoRa" : "AP"; }

} // namespace config
