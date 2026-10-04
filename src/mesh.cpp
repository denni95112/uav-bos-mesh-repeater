#include "mesh.h"

#include <RadioLib.h>
#include <SPI.h>
#include <atomic>
#include <math.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>

#include "pins.h"

#ifndef MESH_CHANNEL_NAME
#define MESH_CHANNEL_NAME "UAV-BOS"
#endif

namespace mesh {

namespace {

using namespace meshproto;

// RadioLib busy-waits through transmissions and CAD. The default yield() never lets lower priority
// tasks run, so a long packet starves the idle task until the task watchdog resets the board.
class TaskYieldHal : public ArduinoHal {
public:
  using ArduinoHal::ArduinoHal;
  void yield() override { vTaskDelay(1); }
};

// Meshtastic modem presets, same order as LoraPreset. Names are the ones Meshtastic hashes for the
// default frequency slot (Channels::getName with an empty primary channel name).
struct PresetParams {
  const char *name;
  float bandwidthKhz;
  uint8_t spreadingFactor;
  uint8_t codingRate; // 4/x
};

const PresetParams kPresets[] = {
    {"ShortFast", 250, 7, 5},  {"ShortSlow", 250, 8, 5}, {"MediumFast", 250, 9, 5}, {"MediumSlow", 250, 10, 5},
    {"LongFast", 250, 11, 5},  {"LongMod", 125, 11, 8},  {"LongSlow", 125, 12, 8},
};
static_assert(sizeof(kPresets) / sizeof(kPresets[0]) == (size_t)LoraPreset::Count, "preset table");

// Meshtastic EU_868 sub-band. 250 kHz presets fit exactly once, 125 kHz presets twice.
constexpr double kBandStartMhz = 869.4;
constexpr double kBandEndMhz = 869.65;
constexpr uint8_t kSyncWord = 0x2B;
constexpr uint16_t kPreambleLen = 16;

// EU 869.4-869.65 MHz allows 10 % duty cycle.
constexpr uint32_t kAirtimeLimitMsPerHour = 360000;
constexpr uint32_t kAirtimeMsPerPercent = kAirtimeLimitMsPerHour / 10;

// Contention window like Meshtastic: weaker links rebroadcast first.
constexpr uint8_t kCwMin = 3;
constexpr uint8_t kCwMax = 8;
constexpr float kSnrMin = -20;
constexpr float kSnrMax = 10;
constexpr uint8_t kMaxCadRetries = 8;

constexpr uint32_t kHeardWindowMs = 30UL * 60 * 1000;
constexpr uint32_t kActiveWindowMs = 5UL * 60 * 1000;
constexpr uint32_t kHourMs = 60UL * 60 * 1000;
constexpr size_t kMaxHeard = 32;
constexpr uint32_t kHelloEveryMs = 15UL * 60 * 1000;
constexpr uint32_t kHelloFirstDelayMs = 30000;

struct OutMsg {
  uint32_t to;
  uint8_t hopLimit;
  uint8_t len;
  uint8_t payload[16];
};

struct PendingRelay {
  bool used = false;
  bool own = false; // packet on the own tracker channel
  uint32_t from = 0;
  uint32_t id = 0;
  uint32_t dueMs = 0;
  uint8_t tries = 0;
  uint8_t len = 0;
  uint8_t buf[kMaxPacketLen];
};

struct SeenPacket {
  uint32_t from;
  uint32_t id;
};

struct AirtimeBucket {
  uint32_t minute = 0;
  uint32_t totalMs = 0;
  uint32_t foreignMs = 0;
};

struct HeardNode {
  uint32_t node = 0;
  uint32_t lastMs = 0;
  uint32_t lastPosMs = 0;
  uint32_t positions = 0;
  uint8_t hops = 0;
  bool viaForeign = false;
  float snr = 0;
  int16_t rssi = 0;
};

enum class TxResult { Sent, Busy, Blocked, Failed };

#if defined(VSPI)
SPIClass loraSpi(VSPI);
#else
SPIClass loraSpi(FSPI);
#endif
SX1276 *radio = nullptr;
TaskHandle_t task = nullptr;
QueueHandle_t txQueue = nullptr;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;

MeshSettings settings;
const PresetParams *preset = &kPresets[(size_t)LoraPreset::LongFast];
float freqMhz = 869.525f;
float slotMs = 0;

uint8_t key[32];
size_t keyLen = 0;
uint8_t chanHash = 0;
uint32_t nodeNum = 0;

std::atomic<bool> wantActive{false};
volatile bool dioFlag = false;

MeshStats st;
HeardNode heard[kMaxHeard];
SeenPacket seen[64];
uint8_t seenNext = 0;
PendingRelay relays[4];
std::atomic<uint32_t> lastOwnQueuedMs{0};
uint32_t maxPacketMs = 0; // time on air of the longest possible packet

AirtimeBucket airtime[60];

void IRAM_ATTR onDio0() {
  dioFlag = true;
  BaseType_t woken = pdFALSE;
  if (task) vTaskNotifyGiveFromISR(task, &woken);
  if (woken) portYIELD_FROM_ISR();
}

uint32_t airtimeLastHourMs(uint32_t *foreignMs = nullptr) {
  uint32_t minute = millis() / 60000;
  uint32_t sum = 0, foreign = 0;
  for (const AirtimeBucket &b : airtime) {
    if (minute - b.minute >= 60) continue;
    sum += b.totalMs;
    foreign += b.foreignMs;
  }
  if (foreignMs) *foreignMs = foreign;
  return sum;
}

void addAirtime(uint32_t ms, bool foreign) {
  uint32_t minute = millis() / 60000;
  AirtimeBucket &b = airtime[minute % 60];
  if (b.minute != minute) {
    b = AirtimeBucket();
    b.minute = minute;
  }
  b.totalMs += ms;
  if (foreign) b.foreignMs += ms;
}

bool alreadySeen(uint32_t from, uint32_t id) {
  for (const SeenPacket &s : seen) {
    if (s.from == from && s.id == id) return true;
  }
  return false;
}

void markSeen(uint32_t from, uint32_t id) {
  seen[seenNext] = {from, id};
  seenNext = (seenNext + 1) % (sizeof(seen) / sizeof(seen[0]));
}

// Call with statsMux held. A relay byte that matches no tracker we know (or still the originator's byte
// after hops, as older Meshtastic firmware leaves it untouched) means a foreign node relayed the packet.
bool relayedByForeign(const Header &h, uint8_t hops) {
  if (!hops) return false;
  if (h.relayNode == (nodeNum & 0xFF)) return false;
  for (const HeardNode &n : heard) {
    if (n.node && n.node != h.from && (n.node & 0xFF) == h.relayNode) return false;
  }
  return true;
}

// Call with statsMux held.
void markHeard(uint32_t node, uint8_t type, uint8_t hops, bool viaForeign, float snr, int16_t rssi) {
  uint32_t now = millis();
  size_t slot = 0;
  bool found = false;
  for (size_t i = 0; i < kMaxHeard; i++) {
    if (heard[i].node == node) {
      slot = i;
      found = true;
      break;
    }
    if (heard[i].lastMs < heard[slot].lastMs) slot = i;
  }
  HeardNode &h = heard[slot];
  if (!found) {
    h = HeardNode();
    h.node = node;
  }
  h.lastMs = now;
  h.hops = hops;
  h.viaForeign = viaForeign;
  h.snr = snr;
  h.rssi = rssi;
  if (type == MsgPosition) {
    h.lastPosMs = now;
    h.positions++;
  }
}

void aesCtr(uint32_t packetId, uint32_t from, uint8_t *data, size_t len) {
  uint8_t nonce[16];
  uint8_t stream[16];
  size_t off = 0;
  makeNonce(packetId, from, nonce);
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, keyLen * 8);
  mbedtls_aes_crypt_ctr(&ctx, len, &off, nonce, stream, data, data);
  mbedtls_aes_free(&ctx);
}

uint32_t cwDelayMs(uint8_t cw) { return (uint32_t)(random(0, 1 << cw) * slotMs); }

uint32_t relayDelayMs(float snr) {
  float t = (constrain(snr, kSnrMin, kSnrMax) - kSnrMin) / (kSnrMax - kSnrMin);
  uint8_t cw = kCwMin + (uint8_t)lroundf(t * (kCwMax - kCwMin));
  return (uint32_t)(2 * kCwMax * slotMs) + cwDelayMs(cw);
}

// djb2, as used by Meshtastic to pick the default frequency slot from the channel name.
uint32_t djb2(const char *s) {
  uint32_t h = 5381;
  while (*s) h = (h << 5) + h + (uint8_t)*s++;
  return h;
}

float slotFrequencyMhz(const PresetParams &p, uint8_t slot) {
  double bwMhz = p.bandwidthKhz / 1000.0;
  uint32_t slots = (uint32_t)lround((kBandEndMhz - kBandStartMhz) / bwMhz);
  if (slots < 1) slots = 1;
  uint32_t index = slot ? (uint32_t)(slot - 1) % slots : djb2(p.name) % slots;
  return (float)(kBandStartMhz + bwMhz / 2 + index * bwMhz);
}

bool foreignBudgetLeft() {
  return airtimeLastHourMs() < (uint32_t)settings.foreignAirtimePct * kAirtimeMsPerPercent;
}

void startRx() {
  dioFlag = false;
  radio->startReceive();
}

// CAD puts the radio into standby, which would abort a packet that is being received right now.
// The header-valid flag stays set until the packet is read, or forever if the payload never arrives.
bool receiving() {
  static uint32_t sinceMs = 0;
  if (!(radio->getIrqFlags() & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_VALID_HEADER)) {
    sinceMs = 0;
    return false;
  }
  uint32_t now = millis();
  if (!sinceMs) sinceMs = now | 1;
  if (now - sinceMs < maxPacketMs) return true;
  sinceMs = 0;
  startRx();
  return false;
}

TxResult transmit(uint8_t *buf, size_t len, bool foreign) {
  uint32_t toa = radio->getTimeOnAir(len) / 1000;
  if (airtimeLastHourMs() + toa > kAirtimeLimitMsPerHour) return TxResult::Blocked;
  if (receiving()) return TxResult::Busy;

  int16_t cad = radio->scanChannel();
  if (cad == RADIOLIB_LORA_DETECTED) {
    startRx();
    return TxResult::Busy;
  }

  int16_t res = radio->transmit(buf, len);
  addAirtime(toa, foreign);
  startRx();
  if (res != RADIOLIB_ERR_NONE) {
    Serial.printf("[mesh] transmit failed %d\n", res);
    return TxResult::Failed;
  }
  return TxResult::Sent;
}

// Own-channel packets may take the slot of a queued foreign packet.
void scheduleRelay(const uint8_t *buf, size_t len, const Header &h, float snr, bool own) {
  PendingRelay *slot = nullptr;
  for (PendingRelay &r : relays) {
    if (!r.used) {
      slot = &r;
      break;
    }
  }
  if (!slot && own) {
    for (PendingRelay &r : relays) {
      if (!r.own) {
        slot = &r;
        break;
      }
    }
    if (slot) {
      portENTER_CRITICAL(&statsMux);
      st.foreignDropped++;
      portEXIT_CRITICAL(&statsMux);
    }
  }
  if (!slot) return;

  Header out = h;
  out.hopLimit = h.hopLimit - 1;
  out.relayNode = nodeNum & 0xFF;
  out.nextHop = 0; // we keep no routes, like a Meshtastic relay without a next hop for the destination
  memcpy(slot->buf, buf, len);
  writeHeader(out, slot->buf);
  slot->len = len;
  slot->own = own;
  slot->from = h.from;
  slot->id = h.id;
  slot->tries = 0;
  slot->dueMs = millis() + relayDelayMs(snr);
  slot->used = true;
}

void cancelRelay(uint32_t from, uint32_t id) {
  for (PendingRelay &r : relays) {
    if (r.used && r.from == from && r.id == id) r.used = false;
  }
}

void noteTracker(const Header &h, const uint8_t *payload, size_t len, float snr, int16_t rssi) {
  uint8_t type = messageType(payload, len);
  if (!type) return;
  uint8_t hops = h.hopStart >= h.hopLimit ? h.hopStart - h.hopLimit : 0;

  portENTER_CRITICAL(&statsMux);
  st.rxCount++;
  bool viaForeign = relayedByForeign(h, hops);
  if (viaForeign) st.viaForeignCount++;
  markHeard(h.from, type, hops, viaForeign, snr, rssi);
  portEXIT_CRITICAL(&statsMux);
}

void handleRx() {
  uint32_t irq = radio->getIrqFlags();
  // DIO0 is shared by RxDone, TxDone and CAD. Only a finished reception is a packet.
  if (!(irq & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE)) return;

  uint8_t buf[kMaxPacketLen + 1];
  size_t len = radio->getPacketLength();
  if (len > kMaxPacketLen) len = kMaxPacketLen;
  int16_t res = radio->readData(buf, len);
  float snr = radio->getSNR();
  int16_t rssi = (int16_t)radio->getRSSI();
  startRx();
  if (res != RADIOLIB_ERR_NONE || len < kHeaderLen) return;

  Header h = readHeader(buf);
  if (h.from == 0 || h.from == nodeNum) return;

  if (alreadySeen(h.from, h.id)) {
    // Someone else relayed it already, no need for us to add another copy.
    cancelRelay(h.from, h.id);
    return;
  }
  markSeen(h.from, h.id);

  portENTER_CRITICAL(&statsMux);
  st.lastRxMs = millis();
  st.lastRssi = rssi;
  st.lastSnr = snr;
  portEXIT_CRITICAL(&statsMux);

  bool forUs = h.to == nodeNum;

  uint8_t plain[kMaxPacketLen];
  const uint8_t *payload = nullptr;
  size_t payloadLen = 0;
  uint8_t type = 0;
  if (h.channel == chanHash) {
    size_t plainLen = len - kHeaderLen;
    memcpy(plain, buf + kHeaderLen, plainLen);
    aesCtr(h.id, h.from, plain, plainLen);
    uint32_t port = 0;
    if (decodeData(plain, plainLen, port, payload, payloadLen) && port == kPortPrivateApp) {
      type = messageType(payload, payloadLen);
    }
  }
  // The channel hash is a single byte, so only a packet that decrypts to a tracker message is ours.
  bool ownChannel = type != 0;

  bool relayable = !forUs && h.hopLimit > 0 && (h.nextHop == 0 || h.nextHop == (nodeNum & 0xFF));
  if (relayable && ownChannel && settings.relayMode != RelayMode::None) {
    scheduleRelay(buf, len, h, snr, true);
  } else if (relayable && !ownChannel && settings.relayMode == RelayMode::All) {
    if (foreignBudgetLeft()) {
      scheduleRelay(buf, len, h, snr, false);
    } else {
      portENTER_CRITICAL(&statsMux);
      st.foreignDropped++;
      portEXIT_CRITICAL(&statsMux);
    }
  }

  if (ownChannel && (h.to == kBroadcast || forUs)) noteTracker(h, payload, payloadLen, snr, rssi);
}

PendingRelay *nextDueRelay() {
  uint32_t now = millis();
  PendingRelay *pick = nullptr;
  for (PendingRelay &r : relays) {
    if (!r.used || (int32_t)(now - r.dueMs) < 0) continue;
    if (!pick || (r.own && !pick->own)) pick = &r;
  }
  return pick;
}

// At most one transmission per call so received packets are handled in between.
void processRelays() {
  PendingRelay *r = nextDueRelay();
  if (!r) return;
  if (!r->own && !foreignBudgetLeft()) {
    r->used = false;
    portENTER_CRITICAL(&statsMux);
    st.foreignDropped++;
    portEXIT_CRITICAL(&statsMux);
    return;
  }
  TxResult res = transmit(r->buf, r->len, !r->own);
  if (res == TxResult::Busy && ++r->tries < kMaxCadRetries) {
    r->dueMs = millis() + cwDelayMs(kCwMax);
    return;
  }
  r->used = false;
  if (res == TxResult::Sent) {
    portENTER_CRITICAL(&statsMux);
    st.relayCount++;
    if (r->own) st.ownRelayCount++;
    else st.foreignRelayCount++;
    portEXIT_CRITICAL(&statsMux);
  }
}

void enqueue(uint32_t to, const uint8_t *payload, size_t len, uint8_t hopLimit) {
  if (!txQueue || !len || len > sizeof(OutMsg::payload)) return;
  OutMsg m;
  m.to = to;
  m.hopLimit = hopLimit;
  m.len = len;
  memcpy(m.payload, payload, len);
  if (xQueueSend(txQueue, &m, 0) == pdTRUE) lastOwnQueuedMs = millis();
}

// Lets neighbours recognise this node as a tracker relay. Hop limit 0, so the Hello is not flooded.
void maybeHello(uint32_t now) {
  static bool started = false;
  if (!started) {
    started = true;
    if (!lastOwnQueuedMs) lastOwnQueuedMs = now - kHelloEveryMs + kHelloFirstDelayMs;
  }
  if (settings.relayMode == RelayMode::None || now - lastOwnQueuedMs < kHelloEveryMs) return;
  uint8_t buf[4];
  size_t n = encodeHello(buf, sizeof(buf));
  if (n) enqueue(kBroadcast, buf, n, 0);
}

void processOwn() {
  static OutMsg msg;
  static bool havePending = false;
  static uint32_t notBeforeMs = 0;
  static uint8_t tries = 0;
  static uint8_t packet[kMaxPacketLen];
  static size_t packetLen = 0;

  if (!havePending) {
    if (xQueueReceive(txQueue, &msg, 0) != pdTRUE) return;
    Header h;
    h.to = msg.to;
    h.from = nodeNum;
    do {
      h.id = esp_random();
    } while (h.id == 0);
    h.hopLimit = msg.hopLimit;
    h.hopStart = msg.hopLimit;
    h.channel = chanHash;
    h.relayNode = nodeNum & 0xFF;
    writeHeader(h, packet);
    size_t dataLen = encodeData(kPortPrivateApp, msg.payload, msg.len, packet + kHeaderLen, sizeof(packet) - kHeaderLen);
    if (!dataLen) return;
    aesCtr(h.id, nodeNum, packet + kHeaderLen, dataLen);
    packetLen = kHeaderLen + dataLen;
    markSeen(nodeNum, h.id);
    havePending = true;
    tries = 0;
    notBeforeMs = millis() + cwDelayMs(kCwMin);
  }
  if ((int32_t)(millis() - notBeforeMs) < 0) return;

  TxResult res = transmit(packet, packetLen, false);
  if (res == TxResult::Busy && ++tries < kMaxCadRetries) {
    notBeforeMs = millis() + cwDelayMs(kCwMax);
    return;
  }
  havePending = false;

  portENTER_CRITICAL(&statsMux);
  if (res == TxResult::Sent) {
    st.txCount++;
    st.lastTxMs = millis();
  } else if (res == TxResult::Blocked) {
    st.txBlocked++;
  }
  portEXIT_CRITICAL(&statsMux);
}

void meshTask(void *) {
  bool active = false;
  for (;;) {
    bool want = wantActive;
    if (want != active) {
      active = want;
      if (active) {
        startRx();
      } else {
        radio->sleep();
        for (PendingRelay &r : relays) r.used = false;
        xQueueReset(txQueue);
      }
      portENTER_CRITICAL(&statsMux);
      st.active = active;
      portEXIT_CRITICAL(&statsMux);
      Serial.printf("[mesh] radio %s\n", active ? "receiving" : "asleep");
    }
    if (!active) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    if (dioFlag) {
      dioFlag = false;
      handleRx();
    }
    processRelays();
    maybeHello(millis());
    processOwn();

    uint32_t foreignMs = 0;
    uint32_t totalMs = airtimeLastHourMs(&foreignMs);
    portENTER_CRITICAL(&statsMux);
    st.airtimePercent = totalMs * 100.0f / kHourMs;
    st.trackerAirtimePercent = (totalMs - foreignMs) * 100.0f / kHourMs;
    portEXIT_CRITICAL(&statsMux);
  }
}

} // namespace

void begin(const MeshSettings &s, const String &keyB64) {
  if (radio) return;

  settings = config::sanitize(s);
  preset = &kPresets[(size_t)settings.preset];
  freqMhz = slotFrequencyMhz(*preset, settings.slot);
  slotMs = 2.5f * ((1 << preset->spreadingFactor) / preset->bandwidthKhz) + 7.2f;
  st.presetName = preset->name;
  st.freqMhz = freqMhz;
  st.bandwidthKhz = preset->bandwidthKhz;
  st.spreadingFactor = preset->spreadingFactor;
  st.codingRate = preset->codingRate;
  st.hopLimit = settings.hopLimit;
  st.txPowerDbm = settings.txPowerDbm;

  uint64_t mac = ESP.getEfuseMac();
  for (int i = 2; i < 6; i++) nodeNum = (nodeNum << 8) | ((mac >> (8 * i)) & 0xFF);
  st.nodeNum = nodeNum;
  st.placeholderKey = keyB64 == config::kPlaceholderMeshKey;

  size_t olen = 0;
  int kr = mbedtls_base64_decode(key, sizeof(key), &olen, (const unsigned char *)keyB64.c_str(), keyB64.length());
  if (kr != 0 || (olen != 16 && olen != 32)) {
    st.error = "Mesh-Schluessel ungueltig";
    Serial.println("[mesh] mesh key must be a base64 encoded 16 or 32 byte key");
    return;
  }
  keyLen = olen;
  chanHash = channelHash(MESH_CHANNEL_NAME, key, keyLen);
  if (st.placeholderKey) Serial.println("[mesh] WARNING: default mesh key in use, set one in the settings");

  loraSpi.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
  radio = new SX1276(new Module(new TaskYieldHal(loraSpi), PIN_LORA_CS, PIN_LORA_DIO0, PIN_LORA_RST, PIN_LORA_DIO1));
  // begin() accepts up to 17 dBm; 18-20 dBm needs the high-power PA path afterwards.
  int8_t bootPower = settings.txPowerDbm > 17 ? 17 : settings.txPowerDbm;
  int16_t res = radio->begin(freqMhz, preset->bandwidthKhz, preset->spreadingFactor, preset->codingRate, kSyncWord,
                             bootPower, kPreambleLen, 0);
  if (res == RADIOLIB_ERR_NONE && settings.txPowerDbm != bootPower) res = radio->setOutputPower(settings.txPowerDbm);
  if (res != RADIOLIB_ERR_NONE) {
    st.error = "LoRa-Funk nicht gefunden";
    Serial.printf("[mesh] radio init failed %d\n", res);
    delete radio;
    radio = nullptr;
    return;
  }
  radio->setCurrentLimit(120);
  radio->setPacketReceivedAction(onDio0);
  radio->sleep();
  maxPacketMs = radio->getTimeOnAir(255) / 1000 + 100;

  txQueue = xQueueCreate(4, sizeof(OutMsg));
  st.ok = true;
  xTaskCreatePinnedToCore(meshTask, "mesh", 8192, nullptr, 3, &task, 0);
  Serial.printf("[mesh] node !%08lx, channel %s (hash 0x%02x), %s %.4f MHz SF%u BW%.0f CR4/%u, %d dBm, %u hops\n",
                (unsigned long)nodeNum, MESH_CHANNEL_NAME, chanHash, preset->name, freqMhz, preset->spreadingFactor,
                preset->bandwidthKhz, preset->codingRate, settings.txPowerDbm, settings.hopLimit);
}

void setActive(bool on) { wantActive = on && radio; }

MeshStats stats() {
  portENTER_CRITICAL(&statsMux);
  MeshStats s = st;
  uint32_t now = millis();
  s.heardNodes = s.activeNodes = s.directNodes = s.positionNodes1h = 0;
  for (const HeardNode &h : heard) {
    if (!h.node) continue;
    uint32_t age = now - h.lastMs;
    if (age < kHeardWindowMs) s.heardNodes++;
    if (age < kActiveWindowMs) {
      s.activeNodes++;
      if (h.hops == 0) s.directNodes++;
    }
    if (h.positions && now - h.lastPosMs < kHourMs) s.positionNodes1h++;
  }
  portEXIT_CRITICAL(&statsMux);
  return s;
}

size_t nodes(MeshNodeInfo *out, size_t max) {
  HeardNode copy[kMaxHeard];
  portENTER_CRITICAL(&statsMux);
  memcpy(copy, heard, sizeof(copy));
  portEXIT_CRITICAL(&statsMux);

  uint32_t now = millis();
  size_t n = 0;
  for (const HeardNode &h : copy) {
    if (!h.node || now - h.lastMs >= kHourMs) continue;
    MeshNodeInfo info;
    info.node = h.node;
    info.lastAgoSec = (now - h.lastMs) / 1000;
    info.lastPosAgoSec = h.positions ? (int32_t)((now - h.lastPosMs) / 1000) : -1;
    info.positions = h.positions;
    info.hops = h.hops;
    info.viaForeign = h.viaForeign;
    info.snr = h.snr;
    info.rssi = h.rssi;

    size_t pos = n < max ? n : max;
    while (pos > 0 && out[pos - 1].lastAgoSec > info.lastAgoSec) {
      if (pos < max) out[pos] = out[pos - 1];
      pos--;
    }
    if (pos < max) out[pos] = info;
    if (n < max) n++;
  }
  return n;
}

} // namespace mesh
