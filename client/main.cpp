// vnc_client tcp://host:5900 [--frames N] [--out file.ppm]
// Headless viewer for now: connects, applies updates and optionally dumps the
// last frame as PPM. A GUI front-end will sit on top of the same loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "vnc/protocol.h"
#include "vnc/tile_diff.h"
#include "vnc/transport.h"

using namespace vnc;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: vnc_client tcp://host:port [--frames N] [--out file.ppm]\n");
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
