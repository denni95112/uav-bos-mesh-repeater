#pragma once

// Pure encode/decode for the LoRa mesh, no Arduino dependencies.
//
// On air every packet is a Meshtastic packet: 16-byte header + AES-CTR encrypted protobuf
// `Data { portnum = PRIVATE_APP, payload }`. The payload is one of the tracker messages below.

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace meshproto {

constexpr uint32_t kBroadcast = 0xFFFFFFFF;
constexpr size_t kHeaderLen = 16;
constexpr size_t kMaxPacketLen = 255;
constexpr uint32_t kPortPrivateApp = 256;
constexpr size_t kMaxUrlLen = 200;

// Meshtastic PacketHeader flags
constexpr uint8_t kFlagHopLimitMask = 0x07;
constexpr uint8_t kFlagWantAck = 0x08;
constexpr uint8_t kFlagViaMqtt = 0x10;
constexpr uint8_t kFlagHopStartShift = 5;

struct Header {
  uint32_t to = kBroadcast;
  uint32_t from = 0;
  uint32_t id = 0;
  uint8_t hopLimit = 0;
  uint8_t hopStart = 0;
  bool wantAck = false;
  bool viaMqtt = false;
  uint8_t channel = 0;   // channel hash
  uint8_t nextHop = 0;   // 0 = flooding
  uint8_t relayNode = 0; // last byte of the node that sent this copy
};

void writeHeader(const Header &h, uint8_t *out);
Header readHeader(const uint8_t *in);

// Meshtastic channel hash: XOR of all name bytes XOR all key bytes.
uint8_t channelHash(const char *name, const uint8_t *key, size_t keyLen);

// Meshtastic AES-CTR nonce: packet id (u64 LE), sender node (u32 LE), 4 zero bytes.
void makeNonce(uint32_t packetId, uint32_t fromNode, uint8_t nonce[16]);

// protobuf `Data`: field 1 portnum (varint), field 2 payload (bytes). Returns the encoded length, 0 if too large.
size_t encodeData(uint32_t portnum, const uint8_t *payload, size_t len, uint8_t *out, size_t outCap);
// Unknown fields are skipped. `payload` points into `in`.
bool decodeData(const uint8_t *in, size_t len, uint32_t &portnum, const uint8_t *&payload, size_t &payloadLen);

// ---- Tracker messages inside PRIVATE_APP ----

constexpr uint8_t kVersion = 1;

enum MsgType : uint8_t {
  MsgPosition = 0x01,
  MsgCredentials = 0x02,
  MsgCredRequest = 0x03,
  MsgHello = 0x04, // not relayed, lets neighbours recognise this node as a tracker relay
};

struct Position {
  double latitude = 0;
  double longitude = 0;
  float altitude = 0; // m
  float speedMps = 0;
  float heading = 0;  // deg
  float accuracy = 0; // m
  uint32_t fixTime = 0; // unix seconds, 0 = unknown
  uint16_t urlHash = 0;
};

constexpr size_t kPositionLen = 24;

// FNV-1a over the URL, folded to 16 bit. Lets a gateway notice a changed URL.
uint16_t urlHash(const std::string &url);

size_t encodePosition(const Position &p, uint8_t *out, size_t outCap);
size_t encodeCredentials(const std::string &url, uint8_t *out, size_t outCap);
size_t encodeCredRequest(uint8_t *out, size_t outCap);
size_t encodeHello(uint8_t *out, size_t outCap);

// Returns the message type, 0 if the payload is not a valid tracker message.
uint8_t messageType(const uint8_t *in, size_t len);
bool decodePosition(const uint8_t *in, size_t len, Position &p);
bool decodeCredentials(const uint8_t *in, size_t len, std::string &url, uint16_t &hash);

} // namespace meshproto
