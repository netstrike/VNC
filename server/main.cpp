// vnc_server [--listen tcp://0.0.0.0:5900] [--source screen|pattern] [--fps 60]
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "vnc/capture.h"
#include "vnc/protocol.h"
#include "vnc/tile_diff.h"
#include "vnc/transport.h"

using namespace vnc;
using Clock = std::chrono::steady_clock;

static void serve(ITransport& t, ICaptureSource& source, IInputSink& input, int fps) {
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

int main(int argc, char** argv) {
  std::string listen = "tcp://0.0.0.0:5900", sourceName = "screen";
  int fps = 60;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--listen")) listen = argv[i + 1];
    else if (!std::strcmp(argv[i], "--source")) sourceName = argv[i + 1];
    else if (!std::strcmp(argv[i], "--fps")) fps = std::max(1, std::atoi(argv[i + 1]));
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

  try {
    auto listener = listenOn(listen);
    std::fprintf(stderr, "listening on port %u\n", listener->port());
    for (;;) {
      auto conn = listener->accept();
      std::fprintf(stderr, "client %s connected\n", conn->peer().c_str());
      try {
        serve(*conn, *source, *input, fps);
      } catch (const std::exception& e) {
        std::fprintf(stderr, "client error: %s\n", e.what());
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
