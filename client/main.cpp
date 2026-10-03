// vnc_client tls://host:5900 [--frames N] [--out file.ppm]
// Headless viewer for now: connects, authenticates, applies updates and
// optionally dumps the last frame as PPM. A GUI front-end will sit on top of
// the same loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "vnc/auth.h"
#include "vnc/protocol.h"
#include "vnc/tile_diff.h"
#include "vnc/transport.h"

using namespace vnc;

namespace {

// The password never travels on the wire: it is combined with the server's
// random nonce through PBKDF2+HMAC, exactly as the server computes its own
// side of the check (see auth.h).
void authenticate(ITransport& t, const std::string& password) {
  Message msg = receiveMessage(t);
  if (msg.type != MsgType::AuthChallenge) throw std::runtime_error("expected AuthChallenge");
  AuthChallenge challenge = decodeAuthChallenge(msg.payload);

  // Derive the same key the server has stored, using the salt/iteration
  // count it just sent, then answer its nonce with an HMAC. The password
  // itself never goes on the wire.
  Credentials c;
  c.salt = challenge.salt;
  c.iterations = challenge.iterations;
  deriveKey(c, password);
  auto response = hmacChallenge(c, challenge.nonce);
  sendMessage(t, MsgType::AuthResponse, toPayload(response));

  Message result = receiveMessage(t);
  if (result.type != MsgType::AuthResult || result.payload.size() != 1 || result.payload[0] != 1)
    throw std::runtime_error("authentication failed");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: vnc_client tls://host:port [--frames N] [--out file.ppm]\n");
    return 2;
  }
  int frames = 0;
  std::string out;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--frames")) frames = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
  }

  try {
    auto t = connectTo(argv[1]);

    std::fprintf(stderr, "Password: ");
    std::string password;
    std::getline(std::cin, password);
    authenticate(*t, password);

    sendMessage(*t, MsgType::Hello, encode(Hello{}));
    Message hello = receiveMessage(*t);
    Hello h = decodeHello(hello.payload);
    std::fprintf(stderr, "connected, desktop %ux%u\n", h.width, h.height);

    Frame screen;
    for (int n = 0; frames == 0 || n < frames;) {
      Message m = receiveMessage(*t);
      if (m.type != MsgType::FrameUpdate) continue;
      FrameUpdate u = decodeFrameUpdate(m.payload);
      applyRects(screen, u.width, u.height, u.rects);
      ++n;
    }
    sendMessage(*t, MsgType::Bye, {});

    if (!out.empty()) {
      FILE* f = std::fopen(out.c_str(), "wb");
      if (!f) return 1;
      std::fprintf(f, "P6\n%u %u\n255\n", screen.width, screen.height);
      for (size_t i = 0; i < screen.bgra.size(); i += 4) {
        const uint8_t rgb[3] = {screen.bgra[i + 2], screen.bgra[i + 1], screen.bgra[i]};
        std::fwrite(rgb, 1, 3, f);
      }
      std::fclose(f);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
