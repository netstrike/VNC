#include <cstdio>
#include <cstdlib>
#include <thread>

#include "vnc/capture.h"
#include "vnc/protocol.h"
#include "vnc/tile_diff.h"
#include "vnc/transport.h"

using namespace vnc;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static void testMessages() {
  Hello h = decodeHello(encode(Hello{1, 800, 600}));
  CHECK(h.version == 1 && h.width == 800 && h.height == 600);
  PointerEvent p = decodePointerEvent(encode(PointerEvent{-5, 300, 3, -2}));
  CHECK(p.x == -5 && p.y == 300 && p.buttons == 3 && p.wheel == -2);
  KeyEvent k = decodeKeyEvent(encode(KeyEvent{0x1e, true}));
  CHECK(k.keycode == 0x1e && k.down);
  bool threw = false;
  try { decodeHello({1, 2, 3}); } catch (const std::exception&) { threw = true; }
  CHECK(threw);
}

static void testDiffLossless() {
  auto src = makePatternSource(200, 130);
  Frame a, b, remote;
  src->grab(a);
  applyRects(remote, a.width, a.height, diffTiles(Frame{}, a));
  CHECK(remote.bgra == a.bgra);
  src->grab(b);
  auto rects = diffTiles(a, b);
  CHECK(!rects.empty());
  CHECK(diffTiles(b, b).empty());
  applyRects(remote, b.width, b.height, rects);
  CHECK(remote.bgra == b.bgra);  // lossless round trip
}

static void testTcpLoopback() {
  auto listener = listenOn("tcp://127.0.0.1:0");
  const uint16_t port = listener->port();
  std::thread server([&] {
    auto c = listener->accept();
    Message m = receiveMessage(*c);
    sendMessage(*c, m.type, m.payload);  // echo
  });
  auto c = connectTo("tcp://127.0.0.1:" + std::to_string(port));
  FrameUpdate u;
  u.width = 64; u.height = 64;
  Rect r; r.w = 2; r.h = 2; r.data.assign(16, 0xab);
  u.rects.push_back(r);
  sendMessage(*c, MsgType::FrameUpdate, encode(u));
  Message echo = receiveMessage(*c);
  FrameUpdate back = decodeFrameUpdate(echo.payload);
  CHECK(back.rects.size() == 1 && back.rects[0].data == r.data);
  server.join();
}

int main() {
  testMessages();
  testDiffLossless();
  testTcpLoopback();
  std::puts("all tests passed");
}
