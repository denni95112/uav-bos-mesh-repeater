#include "meshproto.h"

#include <math.h>
#include <string.h>

namespace meshproto {

namespace {

const char kUrlPrefix[] = "https://gps.beta.uav-bos.de/telemetry/objects/";
constexpr uint8_t kCredFlagPrefix = 0x01;

void putU16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = v >> 8;
}

void putU32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF;
}

uint16_t getU16(const uint8_t *p) { return p[0] | (p[1] << 8); }

uint32_t getU32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t clampRound(double v, double lo, double hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return (int32_t)lround(v);
}

size_t putVarint(uint64_t v, uint8_t *out) {
  size_t n = 0;
  do {
    uint8_t b = v & 0x7F;
    v >>= 7;
    out[n++] = b | (v ? 0x80 : 0);
  } while (v);
  return n;
}

bool getVarint(const uint8_t *in, size_t len, size_t &pos, uint64_t &v) {
  v = 0;
  for (int shift = 0; shift < 64 && pos < len; shift += 7) {
    uint8_t b = in[pos++];
    v |= (uint64_t)(b & 0x7F) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

} // namespace

void writeHeader(const Header &h, uint8_t *out) {
  putU32(out, h.to);
  putU32(out + 4, h.from);
  putU32(out + 8, h.id);
  out[12] = (h.hopLimit & kFlagHopLimitMask) | (h.wantAck ? kFlagWantAck : 0) | (h.viaMqtt ? kFlagViaMqtt : 0) |
            ((h.hopStart & 0x07) << kFlagHopStartShift);
  out[13] = h.channel;
  out[14] = h.nextHop;
  out[15] = h.relayNode;
}

Header readHeader(const uint8_t *in) {
  Header h;
  h.to = getU32(in);
  h.from = getU32(in + 4);
  h.id = getU32(in + 8);
  h.hopLimit = in[12] & kFlagHopLimitMask;
  h.wantAck = in[12] & kFlagWantAck;
  h.viaMqtt = in[12] & kFlagViaMqtt;
  h.hopStart = (in[12] >> kFlagHopStartShift) & 0x07;
  h.channel = in[13];
  h.nextHop = in[14];
  h.relayNode = in[15];
  return h;
}

uint8_t channelHash(const char *name, const uint8_t *key, size_t keyLen) {
  uint8_t h = 0;
  for (const char *c = name; *c; c++) h ^= (uint8_t)*c;
  for (size_t i = 0; i < keyLen; i++) h ^= key[i];
  return h;
}

void makeNonce(uint32_t packetId, uint32_t fromNode, uint8_t nonce[16]) {
  memset(nonce, 0, 16);
  putU32(nonce, packetId);
  putU32(nonce + 8, fromNode);
}

size_t encodeData(uint32_t portnum, const uint8_t *payload, size_t len, uint8_t *out, size_t outCap) {
  uint8_t tmp[kMaxPacketLen + 16];
  size_t n = 0;
  tmp[n++] = 0x08; // field 1, varint
  n += putVarint(portnum, tmp + n);
  tmp[n++] = 0x12; // field 2, length delimited
  n += putVarint(len, tmp + n);
  if (n + len > outCap || n + len > sizeof(tmp)) return 0;
  memcpy(out, tmp, n);
  memcpy(out + n, payload, len);
  return n + len;
}

bool decodeData(const uint8_t *in, size_t len, uint32_t &portnum, const uint8_t *&payload, size_t &payloadLen) {
  size_t pos = 0;
  bool havePort = false;
  payload = nullptr;
  payloadLen = 0;
  while (pos < len) {
    uint64_t key;
    if (!getVarint(in, len, pos, key)) return false;
    uint32_t field = key >> 3;
    uint8_t wire = key & 0x07;
    uint64_t v;
    switch (wire) {
    case 0:
      if (!getVarint(in, len, pos, v)) return false;
      if (field == 1) {
        portnum = (uint32_t)v;
        havePort = true;
      }
      break;
    case 1:
      if (pos + 8 > len) return false;
      pos += 8;
      break;
    case 2:
      if (!getVarint(in, len, pos, v) || pos + v > len) return false;
      if (field == 2) {
        payload = in + pos;
        payloadLen = (size_t)v;
      }
      pos += (size_t)v;
      break;
    case 5:
      if (pos + 4 > len) return false;
      pos += 4;
      break;
    default:
      return false;
    }
  }
  return havePort && payload;
}

uint16_t urlHash(const std::string &url) {
  uint32_t h = 2166136261u;
  for (char c : url) {
    h ^= (uint8_t)c;
    h *= 16777619u;
  }
  return (uint16_t)((h >> 16) ^ (h & 0xFFFF));
}

size_t encodePosition(const Position &p, uint8_t *out, size_t outCap) {
  if (outCap < kPositionLen) return 0;
  out[0] = MsgPosition;
  out[1] = kVersion;
  putU32(out + 2, (uint32_t)clampRound(p.latitude * 1e7, -900000000.0, 900000000.0));
  putU32(out + 6, (uint32_t)clampRound(p.longitude * 1e7, -1800000000.0, 1800000000.0));
  putU16(out + 10, (uint16_t)(int16_t)clampRound(p.altitude, -32768, 32767));
  putU16(out + 12, (uint16_t)clampRound(p.speedMps * 100.0, 0, 65535));
  putU16(out + 14, (uint16_t)clampRound(fmod(p.heading + 360.0, 360.0) * 100.0, 0, 35999));
  putU16(out + 16, (uint16_t)clampRound(p.accuracy * 10.0, 0, 65535));
  putU32(out + 18, p.fixTime);
  putU16(out + 22, p.urlHash);
  return kPositionLen;
}

size_t encodeCredentials(const std::string &url, uint8_t *out, size_t outCap) {
  if (url.empty() || url.size() > kMaxUrlLen) return 0;
  uint8_t flags = 0;
  std::string body = url;
  const size_t prefixLen = sizeof(kUrlPrefix) - 1;
  if (url.size() > prefixLen && url.compare(0, prefixLen, kUrlPrefix) == 0) {
    flags |= kCredFlagPrefix;
    body = url.substr(prefixLen);
  }
  size_t n = 5 + body.size();
  if (n > outCap) return 0;
  out[0] = MsgCredentials;
  out[1] = kVersion;
  putU16(out + 2, urlHash(url));
  out[4] = flags;
  memcpy(out + 5, body.data(), body.size());
  return n;
}

size_t encodeCredRequest(uint8_t *out, size_t outCap) {
  if (outCap < 2) return 0;
  out[0] = MsgCredRequest;
  out[1] = kVersion;
  return 2;
}

size_t encodeHello(uint8_t *out, size_t outCap) {
  if (outCap < 2) return 0;
  out[0] = MsgHello;
  out[1] = kVersion;
  return 2;
}

uint8_t messageType(const uint8_t *in, size_t len) {
  if (len < 2 || in[1] != kVersion) return 0;
  switch (in[0]) {
  case MsgPosition: return len >= kPositionLen ? MsgPosition : 0;
  case MsgCredentials: return len > 5 ? MsgCredentials : 0;
  case MsgCredRequest: return MsgCredRequest;
  case MsgHello: return MsgHello;
  default: return 0;
  }
}

bool decodePosition(const uint8_t *in, size_t len, Position &p) {
  if (messageType(in, len) != MsgPosition) return false;
  p.latitude = (int32_t)getU32(in + 2) / 1e7;
  p.longitude = (int32_t)getU32(in + 6) / 1e7;
  p.altitude = (int16_t)getU16(in + 10);
  p.speedMps = getU16(in + 12) / 100.0f;
  p.heading = getU16(in + 14) / 100.0f;
  p.accuracy = getU16(in + 16) / 10.0f;
  p.fixTime = getU32(in + 18);
  p.urlHash = getU16(in + 22);
  return true;
}

bool decodeCredentials(const uint8_t *in, size_t len, std::string &url, uint16_t &hash) {
  if (messageType(in, len) != MsgCredentials) return false;
  hash = getU16(in + 2);
  uint8_t flags = in[4];
  url.assign((flags & kCredFlagPrefix) ? kUrlPrefix : "");
  url.append((const char *)in + 5, len - 5);
  if (url.size() > kMaxUrlLen || urlHash(url) != hash) return false;
  return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}

} // namespace meshproto
