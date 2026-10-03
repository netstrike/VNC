#include "vnc/protocol.h"

#include <cstring>
#include <stdexcept>

namespace vnc {
namespace {

class Writer {
public:
  void u8(uint8_t v) { buf_.push_back(v); }
  void u16(uint16_t v) { u8(v & 0xff); u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xffff); u16(v >> 16); }
  void bytes(const void* p, size_t n) {
    auto* b = static_cast<const uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  std::vector<uint8_t> take() { return std::move(buf_); }

private:
  std::vector<uint8_t> buf_;
};

class Reader {
public:
  explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}
  uint8_t u8() { need(1); return b_[pos_++]; }
  uint16_t u16() { uint16_t lo = u8(); return lo | (uint16_t(u8()) << 8); }
  uint32_t u32() { uint32_t lo = u16(); return lo | (uint32_t(u16()) << 16); }
  void bytes(void* out, size_t n) {
    need(n);
    if (n) std::memcpy(out, b_.data() + pos_, n);
    pos_ += n;
  }
  size_t remaining() const { return b_.size() - pos_; }

private:
  void need(size_t n) const {
    if (b_.size() - pos_ < n) throw std::runtime_error("protocol: truncated message");
  }
  const std::vector<uint8_t>& b_;
  size_t pos_ = 0;
};

}  // namespace

std::vector<uint8_t> encode(const Hello& h) {
  Writer w;
  w.u32(kMagic);
  w.u16(h.version);
  w.u16(h.width);
  w.u16(h.height);
  return w.take();
}

Hello decodeHello(const std::vector<uint8_t>& p) {
  Reader r(p);
  if (r.u32() != kMagic) throw std::runtime_error("protocol: bad magic");
  Hello h;
  h.version = r.u16();
  h.width = r.u16();
  h.height = r.u16();
  return h;
}

std::vector<uint8_t> encode(const FrameUpdate& f) {
  Writer w;
  w.u16(f.width);
  w.u16(f.height);
  w.u32(static_cast<uint32_t>(f.rects.size()));
  for (const Rect& r : f.rects) {
    w.u16(r.x); w.u16(r.y); w.u16(r.w); w.u16(r.h);
    w.u8(static_cast<uint8_t>(r.encoding));
    w.u32(static_cast<uint32_t>(r.data.size()));
    w.bytes(r.data.data(), r.data.size());
  }
  return w.take();
}

FrameUpdate decodeFrameUpdate(const std::vector<uint8_t>& p) {
  Reader r(p);
  FrameUpdate f;
  f.width = r.u16();
  f.height = r.u16();
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n; ++i) {
    Rect rect;
    rect.x = r.u16(); rect.y = r.u16(); rect.w = r.u16(); rect.h = r.u16();
    rect.encoding = static_cast<Encoding>(r.u8());
    uint32_t len = r.u32();
    if (len > r.remaining()) throw std::runtime_error("protocol: bad rect length");
    rect.data.resize(len);
    r.bytes(rect.data.data(), len);
    f.rects.push_back(std::move(rect));
  }
  return f;
}

std::vector<uint8_t> encode(const KeyEvent& k) {
  Writer w;
  w.u32(k.keycode);
  w.u8(k.down ? 1 : 0);
  return w.take();
}

KeyEvent decodeKeyEvent(const std::vector<uint8_t>& p) {
  Reader r(p);
  KeyEvent k;
  k.keycode = r.u32();
  k.down = r.u8() != 0;
  return k;
}

std::vector<uint8_t> encode(const PointerEvent& e) {
  Writer w;
  w.u32(static_cast<uint32_t>(e.x));
  w.u32(static_cast<uint32_t>(e.y));
  w.u8(e.buttons);
  w.u16(static_cast<uint16_t>(e.wheel));
  return w.take();
}

PointerEvent decodePointerEvent(const std::vector<uint8_t>& p) {
  Reader r(p);
  PointerEvent e;
  e.x = static_cast<int32_t>(r.u32());
  e.y = static_cast<int32_t>(r.u32());
  e.buttons = r.u8();
  e.wheel = static_cast<int16_t>(r.u16());
  return e;
}

std::vector<uint8_t> encodeClipboard(const std::string& s) {
  Writer w;
  w.bytes(s.data(), s.size());
  return w.take();
}

std::string decodeClipboard(const std::vector<uint8_t>& p) {
  return std::string(p.begin(), p.end());
}

void sendMessage(ITransport& t, MsgType type, const std::vector<uint8_t>& payload) {
  if (payload.size() > kMaxPayload) throw std::runtime_error("protocol: payload too large");
  uint8_t header[5];
  header[0] = static_cast<uint8_t>(type);
  uint32_t n = static_cast<uint32_t>(payload.size());
  for (int i = 0; i < 4; ++i) header[1 + i] = (n >> (8 * i)) & 0xff;
  // One write for small messages avoids two TCP segments (latency).
  std::vector<uint8_t> out;
  out.reserve(5 + payload.size());
  out.insert(out.end(), header, header + 5);
  out.insert(out.end(), payload.begin(), payload.end());
  t.writeAll(out.data(), out.size());
}

Message receiveMessage(ITransport& t) {
  uint8_t header[5];
  t.readExact(header, 5);
  uint32_t n = 0;
  for (int i = 0; i < 4; ++i) n |= uint32_t(header[1 + i]) << (8 * i);
  if (n > kMaxPayload) throw std::runtime_error("protocol: payload too large");
  Message m;
  m.type = static_cast<MsgType>(header[0]);
  m.payload.resize(n);
  if (n) t.readExact(m.payload.data(), n);
  return m;
}

}  // namespace vnc
