#include <cstdio>
// Screen capture for X11 using XGetImage on the root window of the default
// screen, with XRandR used only to learn monitor geometry (for
// listMonitors()/selectMonitors()) and to crop/compose the result when a
// subset of monitors is selected. Capture itself stays simple and correct;
// an MIT-SHM backed version can replace it later if full-screen XGetImage
// turns out to be the latency bottleneck.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "vnc/capture.h"

namespace vnc {
namespace {

struct XDisplayDeleter { void operator()(Display* d) const { if (d) XCloseDisplay(d); } };

// Bounding box of a set of monitors, in root-window coordinates.
struct Box { int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0; };

class X11Capture : public ICaptureSource {
public:
  X11Capture() {
    display_.reset(XOpenDisplay(nullptr));
    if (!display_) throw std::runtime_error("x11: cannot open display (is DISPLAY set?)");
    screen_ = DefaultScreen(display_.get());
    root_ = RootWindow(display_.get(), screen_);
  }

  std::vector<MonitorInfo> listMonitors() override { return queryMonitors(); }

  void selectMonitors(const std::vector<int>& indices) override { selected_ = indices; }

  bool grab(Frame& out) override {
    const int rootW = DisplayWidth(display_.get(), screen_);
    const int rootH = DisplayHeight(display_.get(), screen_);
    XImage* img = XGetImage(display_.get(), root_, 0, 0, rootW, rootH, AllPlanes, ZPixmap);
    if (!img) return false;
    if (!(img->bits_per_pixel == 32 && img->depth >= 24 && img->byte_order == LSBFirst)) {
      // See the comment on the equivalent check below: unsupported pixel
      // format, bail out rather than show a garbled image.
      XDestroyImage(img);
      return false;
    }

    Box box = boundingBoxFor(selected_, rootW, rootH);
    out.width = static_cast<uint16_t>(box.x2 - box.x1);
    out.height = static_cast<uint16_t>(box.y2 - box.y1);
    out.bgra.assign(size_t(out.width) * out.height * 4, 0);

    // Copy only the rows/columns inside the requested box; when it is the
    // whole screen (the common case, no selection) this is a straight copy.
    for (int32_t y = box.y1; y < box.y2; ++y) {
      const uint8_t* src = reinterpret_cast<const uint8_t*>(img->data) +
                           size_t(y) * img->bytes_per_line + size_t(box.x1) * 4;
      uint8_t* dst = &out.bgra[size_t(y - box.y1) * out.width * 4];
      std::memcpy(dst, src, size_t(out.width) * 4);
    }
    for (size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 255;  // alpha
    XDestroyImage(img);
    return true;
  }

private:
  std::vector<MonitorInfo> queryMonitors() {
    std::vector<MonitorInfo> out;
    int n = 0;
    XRRMonitorInfo* mons = XRRGetMonitors(display_.get(), root_, True, &n);
    if (!mons) return out;
    for (int i = 0; i < n; ++i) {
      MonitorInfo m;
      m.index = i;
      m.x = mons[i].x;
      m.y = mons[i].y;
      m.width = static_cast<uint16_t>(mons[i].width);
      m.height = static_cast<uint16_t>(mons[i].height);
      m.primary = mons[i].primary;
      const char* name = XGetAtomName(display_.get(), mons[i].name);
      m.name = name ? name : ("monitor" + std::to_string(i));
      if (name) XFree(const_cast<char*>(name));
      out.push_back(m);
    }
    XRRFreeMonitors(mons);
    return out;
  }

  // Bounding box of the selected monitors (by listMonitors() index), or the
  // whole root window when `indices` is empty or XRandR is unavailable.
  Box boundingBoxFor(const std::vector<int>& indices, int rootW, int rootH) {
    if (indices.empty()) return Box{0, 0, rootW, rootH};
    auto mons = queryMonitors();
    if (mons.empty()) return Box{0, 0, rootW, rootH};
    Box b{INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
    for (int idx : indices) {
      if (idx < 0 || size_t(idx) >= mons.size()) continue;
      const MonitorInfo& m = mons[idx];
      b.x1 = std::min(b.x1, m.x);
      b.y1 = std::min(b.y1, m.y);
      b.x2 = std::max(b.x2, int32_t(m.x + m.width));
      b.y2 = std::max(b.y2, int32_t(m.y + m.height));
    }
    if (b.x1 > b.x2) return Box{0, 0, rootW, rootH};  // no valid index matched
    b.x1 = std::clamp<int32_t>(b.x1, 0, rootW);
    b.y1 = std::clamp<int32_t>(b.y1, 0, rootH);
    b.x2 = std::clamp<int32_t>(b.x2, 0, rootW);
    b.y2 = std::clamp<int32_t>(b.y2, 0, rootH);
    return b;
  }

  std::unique_ptr<Display, XDisplayDeleter> display_;
  int screen_ = 0;
  Window root_ = 0;
  std::vector<int> selected_;
};

}  // namespace

std::unique_ptr<ICaptureSource> makeScreenSource() {
  try {
    return std::make_unique<X11Capture>();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "x11: screen capture unavailable: %s\n", e.what());
    return nullptr;
  }
}

}  // namespace vnc
