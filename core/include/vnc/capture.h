#pragma once
#include <memory>
#include <string>

#include "vnc/protocol.h"
#include "vnc/tile_diff.h"

namespace vnc {

// Source of desktop frames (BGRA8, tightly packed).
class ICaptureSource {
public:
  virtual ~ICaptureSource() = default;
  // Fills `out` with the current desktop. Returns false if nothing could be
  // captured right now (the caller retries later).
  virtual bool grab(Frame& out) = 0;
};

// Receives remote input events.
class IInputSink {
public:
  virtual ~IInputSink() = default;
  virtual void key(const KeyEvent&) = 0;
  virtual void pointer(const PointerEvent&) = 0;
};

// Moving gradient + box, handy for tests and for platforms without a real
// capture backend yet.
std::unique_ptr<ICaptureSource> makePatternSource(uint16_t w, uint16_t h);
// Discards input.
std::unique_ptr<IInputSink> makeNullInput();

// Real screen capture / input injection (implemented per platform). Return
// nullptr when the platform has no backend.
std::unique_ptr<ICaptureSource> makeScreenSource();
std::unique_ptr<IInputSink> makeScreenInput();

}  // namespace vnc
