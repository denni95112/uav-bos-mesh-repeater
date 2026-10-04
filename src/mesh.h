#pragma once

#include <Arduino.h>

#include "config.h"
#include "meshproto.h"

// Meshtastic-compatible LoRa repeater (EU_868, configurable modem preset, private channel, shared key).
// The radio runs on its own task. This node sends no positions; it relays and announces itself with Hello.

struct MeshStats {
  bool ok = false;           // radio initialised and key valid
  bool active = false;       // radio receiving
  bool placeholderKey = false;
  uint32_t nodeNum = 0;
  uint32_t rxCount = 0;           // own-channel packets decoded
  uint32_t relayCount = 0;        // all relayed packets
  uint32_t ownRelayCount = 0;     // relayed packets on the own tracker channel
  uint32_t foreignRelayCount = 0; // relayed packets of other Meshtastic channels
  uint32_t foreignDropped = 0;    // foreign packets not relayed because of the airtime budget
  uint32_t viaForeignCount = 0;   // own-channel packets whose last relay was not a known tracker
  uint32_t txCount = 0;           // own messages sent (Hello)
  uint32_t txBlocked = 0;         // own messages dropped by the airtime limit
  uint32_t lastTxMs = 0;          // last own Hello, 0 = never
  uint32_t lastRxMs = 0;
  int16_t lastRssi = 0;
  float lastSnr = 0;
  float airtimePercent = 0;        // last hour
  float trackerAirtimePercent = 0; // of that, own messages and own-channel relays
  uint8_t heardNodes = 0;          // trackers heard in the last 30 min
  uint8_t activeNodes = 0;         // trackers heard in the last 5 min
  uint8_t directNodes = 0;         // of those, heard directly (not relayed)
  uint8_t positionNodes1h = 0;     // trackers that sent a position in the last hour
  const char *error = "";
  const char *presetName = "";
  float freqMhz = 0;
  float bandwidthKhz = 0;
  uint8_t spreadingFactor = 0;
  uint8_t codingRate = 0; // 4/x
  uint8_t hopLimit = 0;
  int8_t txPowerDbm = 0;
};

struct MeshNodeInfo {
  uint32_t node = 0;
  uint32_t lastAgoSec = 0;
  int32_t lastPosAgoSec = -1; // -1 = no position yet
  uint32_t positions = 0;     // positions received since boot
  uint8_t hops = 0;           // hops of the last packet, 0 = direct
  bool viaForeign = false;    // last packet was relayed by a non-tracker node (e.g. Meshtastic)
  float snr = 0;
  int16_t rssi = 0;
};

namespace mesh {

// Initialises the radio once and starts the task. Settings take effect on the first call only.
void begin(const MeshSettings &settings, const String &keyB64);
void setActive(bool on);

MeshStats stats();
// Trackers heard in the last hour, most recent first. Returns the number written to `out`.
size_t nodes(MeshNodeInfo *out, size_t max);

} // namespace mesh
