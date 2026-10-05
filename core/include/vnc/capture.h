#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vnc/protocol.h"
#include "vnc/tile_diff.h"

namespace vnc {

// One physical monitor, in the same virtual-desktop coordinate space the
// platform uses for pointer events (SM_XVIRTUALSCREEN on Windows, the root
// window on X11).
struct MonitorInfo {
  int index = 0;
  int32_t x = 0, y = 0;
  uint16_t width = 0, height = 0;
  bool primary = false;
  std::string name;
};

// Source of desktop frames (BGRA8, tightly packed).
class ICaptureSource {
public:
  virtual ~ICaptureSource() = default;
  // Fills `out` with the current desktop. Returns false if nothing could be
  // captured right now (the caller retries later).
  virtual bool grab(Frame& out) = 0;

  // Monitors available to capture. A source with no notion of separate
  // monitors (the pattern source, say) returns an empty list; callers then
  // treat the whole of grab()'s output as a single, unnamed display.
  virtual std::vector<MonitorInfo> listMonitors() { return {}; }

  // Restricts later grab() calls to these monitor indices (from
  // listMonitors()), composed into one frame sized to their bounding box;
  // pixels outside a selected monitor but inside that box (a gap between
  // non-adjacent monitors) read as black. An empty list (the default)
  // means "all monitors".
  virtual void selectMonitors(const std::vector<int>& indices) { (void)indices; }
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
