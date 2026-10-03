#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "vnc/transport.h"

namespace vnc {

// Wire format (all integers little-endian):
//   message = u8 type | u32 payload_len | payload
// The protocol is our own (not RFB) and is lossless: pixels are always BGRA8
// and are only ever compressed with lossless codecs.

constexpr uint32_t kMagic = 0x434e564e;  // "NVNC"
constexpr uint16_t kProtocolVersion = 1;
constexpr uint32_t kMaxPayload = 256u * 1024 * 1024;

enum class MsgType : uint8_t {
  Hello = 1,          // both directions, first message after the TLS handshake
  AuthChallenge = 2,  // server -> client, 16-byte random nonce
  AuthResponse = 3,   // client -> server, HMAC-SHA256(derivedKey, nonce)
  AuthResult = 4,     // server -> client, 1 byte: 1 = ok, 0 = failed
  FrameUpdate = 5,    // server -> client
  KeyEvent = 6,       // client -> server
  PointerEvent = 7,   // client -> server
  Clipboard = 8,       // both directions
  Bye = 9,
};

enum class Encoding : uint8_t {
  Raw = 0,
  // Reserved: Zstd = 1, Lz4 = 2 (lossless, low compression level)
};

struct Hello {
  uint16_t version = kProtocolVersion;
  uint16_t width = 0;   // server: current desktop size, client: 0
  uint16_t height = 0;
};

struct Rect {
  uint16_t x = 0, y = 0, w = 0, h = 0;
  Encoding encoding = Encoding::Raw;
  std::vector<uint8_t> data;  // w*h*4 bytes for Raw
};

struct FrameUpdate {
  uint16_t width = 0, height = 0;  // full desktop size
  std::vector<Rect> rects;
};

struct KeyEvent {
  uint32_t keycode = 0;  // platform independent scancode
  bool down = false;
};

struct PointerEvent {
  int32_t x = 0, y = 0;
  uint8_t buttons = 0;  // bit0 left, bit1 middle, bit2 right
  int16_t wheel = 0;
};

struct Message {
  MsgType type = MsgType::Bye;
  std::vector<uint8_t> payload;
};

// Carries what the client needs to derive the same key the server checks
// against: the password never travels on the wire, so it must be combined
// client-side with the server's PBKDF2 salt/iteration count, the same way
// the server derived it when the password was set (see auth.h).
struct AuthChallenge {
  std::vector<uint8_t> salt;   // PBKDF2 salt, as stored server-side
  uint32_t iterations = 0;
  std::array<uint8_t, 16> nonce{};  // fresh per session, prevents replay
};

// Serialisation
std::vector<uint8_t> encode(const Hello&);
std::vector<uint8_t> encode(const AuthChallenge&);
std::vector<uint8_t> encode(const FrameUpdate&);
std::vector<uint8_t> encode(const KeyEvent&);
std::vector<uint8_t> encode(const PointerEvent&);
std::vector<uint8_t> encodeClipboard(const std::string& utf8);

// Parsing: throw std::runtime_error on malformed input.
Hello decodeHello(const std::vector<uint8_t>&);
AuthChallenge decodeAuthChallenge(const std::vector<uint8_t>&);
FrameUpdate decodeFrameUpdate(const std::vector<uint8_t>&);
KeyEvent decodeKeyEvent(const std::vector<uint8_t>&);
PointerEvent decodePointerEvent(const std::vector<uint8_t>&);
std::string decodeClipboard(const std::vector<uint8_t>&);

void sendMessage(ITransport&, MsgType, const std::vector<uint8_t>& payload);
Message receiveMessage(ITransport&);

// Thin helpers so callers don't hand-roll vector<->array conversions for the
// fixed-size auth messages.
std::vector<uint8_t> toPayload(const std::array<uint8_t, 16>&);
std::vector<uint8_t> toPayload(const std::array<uint8_t, 32>&);
std::array<uint8_t, 16> toNonce(const std::vector<uint8_t>&);
std::array<uint8_t, 32> toDigest(const std::vector<uint8_t>&);

}  // namespace vnc
