#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include "vnc/auth.h"
#include "vnc/capture.h"
#include "vnc/protocol.h"
#include "vnc/tile_diff.h"
#include "vnc/tls_transport.h"
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

static void testAuthRoundTrip() {
  Credentials server = makeCredentials("correct-password");
  auto nonce = randomNonce();

  // Client side: knows only the password, plus the salt/iterations the
  // server would send in a real AuthChallenge.
  Credentials client;
  client.salt = server.salt;
  client.iterations = server.iterations;
  deriveKey(client, "correct-password");
  CHECK(secureEqual(hmacChallenge(server, nonce), hmacChallenge(client, nonce)));

  Credentials wrong;
  wrong.salt = server.salt;
  wrong.iterations = server.iterations;
  deriveKey(wrong, "not-the-password");
  CHECK(!secureEqual(hmacChallenge(server, nonce), hmacChallenge(wrong, nonce)));
}

static void testCredentialsPersist() {
  const std::string dir = std::filesystem::temp_directory_path() / "vnc_test_creds";
  std::filesystem::remove_all(dir);
  Credentials c = makeCredentials("hunter2");
  saveCredentials(c, dir + "/credentials");
  Credentials loaded = loadCredentials(dir + "/credentials");
  CHECK(loaded.iterations == c.iterations);
  CHECK(loaded.salt == c.salt);
  CHECK(loaded.key == c.key);
  std::filesystem::remove_all(dir);
}

static void testTlsLoopbackWithAuth() {
  const std::string dir = std::filesystem::temp_directory_path() / "vnc_test_tls";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  ensureServerCertificate(dir + "/cert.pem", dir + "/key.pem");
  Credentials creds = makeCredentials("hunter2");

  auto listener = tlsListen("127.0.0.1", 0, dir + "/cert.pem", dir + "/key.pem");
  const uint16_t port = listener->port();

  std::thread server([&] {
    auto t = listener->accept();
    AuthChallenge challenge;
    challenge.salt = creds.salt;
    challenge.iterations = creds.iterations;
    challenge.nonce = randomNonce();
    sendMessage(*t, MsgType::AuthChallenge, encode(challenge));
    Message resp = receiveMessage(*t);
    bool ok = secureEqual(toDigest(resp.payload), hmacChallenge(creds, challenge.nonce));
    sendMessage(*t, MsgType::AuthResult, {uint8_t(ok ? 1 : 0)});
    if (ok) {
      Message echo = receiveMessage(*t);
      sendMessage(*t, echo.type, echo.payload);
    }
  });

  auto c = tlsConnect("127.0.0.1", port, dir + "/known_hosts");
  Message msg = receiveMessage(*c);
  CHECK(msg.type == MsgType::AuthChallenge);
  AuthChallenge challenge = decodeAuthChallenge(msg.payload);
  Credentials clientCreds;
  clientCreds.salt = challenge.salt;
  clientCreds.iterations = challenge.iterations;
  deriveKey(clientCreds, "hunter2");
  sendMessage(*c, MsgType::AuthResponse, toPayload(hmacChallenge(clientCreds, challenge.nonce)));
  Message result = receiveMessage(*c);
  CHECK(result.payload.size() == 1 && result.payload[0] == 1);

  sendMessage(*c, MsgType::Bye, {1, 2, 3});
  Message echo = receiveMessage(*c);
  CHECK(echo.payload == std::vector<uint8_t>({1, 2, 3}));
  server.join();

  // Reconnecting must see the same pinned fingerprint, not a fresh prompt.
  CHECK(std::filesystem::exists(dir + "/known_hosts"));
  std::filesystem::remove_all(dir);
}

int main() {
  testMessages();
  testDiffLossless();
  testTcpLoopback();
  testAuthRoundTrip();
  testCredentialsPersist();
  testTlsLoopbackWithAuth();
  std::puts("all tests passed");
}
