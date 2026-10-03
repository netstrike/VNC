// vnc_server [--listen tls://0.0.0.0:5900] [--source screen|pattern] [--fps 60]
//            [--monitors all|<comma-separated indices>]
// vnc_server --set-password          (prompts for a password, stores its
//                                      hash under the config directory)
// vnc_server --list-monitors         (prints the available monitors and exits)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "vnc/auth.h"
#include "vnc/capture.h"
#include "vnc/compress.h"
#include "vnc/protocol.h"
#include "vnc/tile_diff.h"
#include "vnc/transport.h"

using namespace vnc;
using Clock = std::chrono::steady_clock;

namespace {

std::string credentialsPath() { return defaultConfigDir() + "/credentials"; }

// Reads a line from stdin without echoing it, where the terminal supports it;
// falls back to a plain (echoed) read otherwise (e.g. when stdin is a pipe).
std::string readPassword(const char* prompt) {
  std::fprintf(stderr, "%s", prompt);
  std::string pass;
  std::getline(std::cin, pass);
  return pass;
}

int setPassword() {
  std::string p1 = readPassword("Nuova password: ");
  std::string p2 = readPassword("Ripeti la password: ");
  if (p1.empty()) { std::fprintf(stderr, "password vuota, operazione annullata\n"); return 1; }
  if (p1 != p2) { std::fprintf(stderr, "le due password non coincidono\n"); return 1; }
  Credentials c = makeCredentials(p1);
  saveCredentials(c, credentialsPath());
  std::fprintf(stderr, "password salvata in %s\n", credentialsPath().c_str());
  return 0;
}

std::vector<int> parseMonitorList(const std::string& s) {
  std::vector<int> out;
  if (s.empty() || s == "all") return out;  // empty selection means "all"
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ',')) if (!tok.empty()) out.push_back(std::atoi(tok.c_str()));
  return out;
}

int listMonitors(ICaptureSource& source) {
  auto monitors = source.listMonitors();
  if (monitors.empty()) {
    std::fprintf(stderr, "questa piattaforma/sorgente non distingue monitor singoli "
                         "(verra' inviato tutto lo schermo)\n");
    return 0;
  }
  for (const auto& m : monitors) {
    std::fprintf(stderr, "%d: %s  %ux%u @ (%d,%d)%s\n", m.index, m.name.c_str(), m.width,
                m.height, m.x, m.y, m.primary ? "  [primario]" : "");
  }
  return 0;
}

// Authenticates the connecting client with a nonce/HMAC challenge. The
// password itself is never sent, not even over the already-encrypted TLS
// channel. Returns false (and tells the client) if it doesn't check out.
bool authenticate(ITransport& t, const Credentials& creds) {
  AuthChallenge challenge;
  challenge.salt = creds.salt;
  challenge.iterations = creds.iterations;
  challenge.nonce = randomNonce();
  sendMessage(t, MsgType::AuthChallenge, encode(challenge));

  Message resp = receiveMessage(t);
  if (resp.type != MsgType::AuthResponse) throw std::runtime_error("expected AuthResponse");
  auto got = toDigest(resp.payload);
  auto want = hmacChallenge(creds, challenge.nonce);
  bool ok = secureEqual(got, want);
  sendMessage(t, MsgType::AuthResult, {uint8_t(ok ? 1 : 0)});
  return ok;
}

void serve(ITransport& t, const Credentials& creds, ICaptureSource& source, IInputSink& input,
          int fps) {
  if (!authenticate(t, creds)) {
    std::fprintf(stderr, "authentication failed for %s\n", t.peer().c_str());
    return;
  }

  Message hello = receiveMessage(t);
  if (hello.type != MsgType::Hello) throw std::runtime_error("expected Hello");
  if (decodeHello(hello.payload).version != kProtocolVersion)
    throw std::runtime_error("protocol version mismatch");

  Frame cur, prev;
  while (!source.grab(cur)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  sendMessage(t, MsgType::Hello, encode(Hello{kProtocolVersion, cur.width, cur.height}));

  std::atomic<bool> done{false};
  std::thread reader([&] {
    try {
      while (!done) {
        Message m = receiveMessage(t);
        switch (m.type) {
          case MsgType::KeyEvent: input.key(decodeKeyEvent(m.payload)); break;
          case MsgType::PointerEvent: input.pointer(decodePointerEvent(m.payload)); break;
          case MsgType::Bye: done = true; break;
          default: break;
        }
      }
    } catch (const std::exception&) {
      done = true;
    }
  });

  const auto period = std::chrono::microseconds(1'000'000 / fps);
  try {
    while (!done) {
      const auto start = Clock::now();
      if (source.grab(cur)) {
        FrameUpdate u;
        u.width = cur.width;
        u.height = cur.height;
        u.rects = diffTiles(prev, cur);
        for (Rect& r : u.rects) compressRect(r);
        if (!u.rects.empty()) {
          sendMessage(t, MsgType::FrameUpdate, encode(u));
          std::swap(prev, cur);
        }
      }
      std::this_thread::sleep_until(start + period);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "session ended: %s\n", e.what());
  }
  done = true;
  t.close();
  reader.join();
}

}  // namespace

int main(int argc, char** argv) {
  std::string listen = "tls://0.0.0.0:5900", sourceName = "screen", monitorSpec;
  int fps = 60;
  bool doListMonitors = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--set-password")) return setPassword();
    if (!std::strcmp(argv[i], "--list-monitors")) { doListMonitors = true; continue; }
    if (i + 1 >= argc) continue;
    if (!std::strcmp(argv[i], "--listen")) listen = argv[++i];
    else if (!std::strcmp(argv[i], "--source")) sourceName = argv[++i];
    else if (!std::strcmp(argv[i], "--fps")) fps = std::max(1, std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--monitors")) monitorSpec = argv[++i];
  }

  std::unique_ptr<ICaptureSource> source;
  std::unique_ptr<IInputSink> input;
  if (sourceName == "screen") {
    source = makeScreenSource();
    input = makeScreenInput();
    if (!source) {
      std::fprintf(stderr, "no screen capture backend on this platform, use --source pattern\n");
      return 1;
    }
  } else {
    source = makePatternSource(640, 360);
    input = makeNullInput();
  }

  if (doListMonitors) return listMonitors(*source);
  source->selectMonitors(parseMonitorList(monitorSpec));

  Credentials creds;
  try {
    creds = loadCredentials(credentialsPath());
  } catch (const std::exception&) {
    std::fprintf(stderr,
                "nessuna password configurata: esegui prima '%s --set-password'\n", argv[0]);
    return 1;
  }

  try {
    auto listener = listenOn(listen);
    std::fprintf(stderr, "listening on port %u\n", listener->port());
    for (;;) {
      auto conn = listener->accept();
      std::fprintf(stderr, "client %s connected\n", conn->peer().c_str());
      try {
        serve(*conn, creds, *source, *input, fps);
      } catch (const std::exception& e) {
        std::fprintf(stderr, "client error: %s\n", e.what());
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
