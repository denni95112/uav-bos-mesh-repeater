#pragma once

#include <Arduino.h>

enum class RepeaterMode : uint8_t {
  WithWifi = 0,  // LoRa repeater, WiFi is the config AP only
  RadioOnly = 1, // LoRa repeater, WiFi off
};

// Meshtastic modem presets that fit the EU_868 sub-band 869.4-869.65 MHz.
enum class LoraPreset : uint8_t {
  ShortFast = 0,
  ShortSlow = 1,
  MediumFast = 2,
  MediumSlow = 3,
  LongFast = 4,
  LongModerate = 5,
  LongSlow = 6,
  Count
};

enum class RelayMode : uint8_t {
  All = 0,     // like Meshtastic rebroadcast mode ALL
  OwnOnly = 1, // only packets on the own tracker channel
  None = 2,
};

struct MeshSettings {
  LoraPreset preset = LoraPreset::LongFast;
  uint8_t slot = 0; // frequency slot, 0 = Meshtastic default for the preset
  uint8_t hopLimit = 3;
  int8_t txPowerDbm = 20;
  RelayMode relayMode = RelayMode::All;
  uint8_t foreignAirtimePct = 6; // foreign packets are relayed only below this share of airtime
};

struct RepeaterConfig {
  String apPass; // empty = open config AP
  RepeaterMode mode = RepeaterMode::WithWifi;
  MeshSettings mesh;
  String meshKey; // base64 channel key, same on all trackers of an organisation

  bool usesWifi() const { return mode == RepeaterMode::WithWifi; }
};

namespace config {

constexpr uint8_t kMaxSlot = 2;
constexpr uint8_t kMinHopLimit = 1;
constexpr uint8_t kMaxHopLimit = 7; // 3 bits in the Meshtastic header
constexpr int8_t kMinTxPowerDbm = 2;
constexpr int8_t kMaxTxPowerDbm = 20; // SX1276 PA_BOOST maximum
constexpr uint8_t kMaxForeignAirtimePct = 10;

// Used when neither NVS nor the build provides a mesh key; the UI warns about it.
constexpr const char *kPlaceholderMeshKey = "CHANGE+ME+CHANGE+ME+CHANGE+ME+CHANGE+ME+CEE=";

// Clamps every field to its valid range.
MeshSettings sanitize(MeshSettings s);
// Base64 that decodes to a 16 or 32 byte AES key.
bool validMeshKey(const String &b64);

void load(RepeaterConfig &cfg);
void save(const RepeaterConfig &cfg);
void saveMode(RepeaterMode mode);
void clear();

RepeaterMode nextMode(RepeaterMode mode);
const char *modeName(RepeaterMode mode);  // long German name for the UI
const char *modeShort(RepeaterMode mode); // badge text

} // namespace config
