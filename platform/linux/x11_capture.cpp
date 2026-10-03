#include <cstdio>
// Screen capture for X11 using XGetImage on the root window of the default
// screen. Simple and correct; an MIT-SHM backed version can replace this
// later if full-screen XGetImage turns out to be the latency bottleneck.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>

#include <cstring>
#include <stdexcept>

#include "vnc/capture.h"

namespace vnc {
namespace {

struct XDisplayDeleter { void operator()(Display* d) const { if (d) XCloseDisplay(d); } };

class X11Capture : public ICaptureSource {
public:
  X11Capture() {
    display_.reset(XOpenDisplay(nullptr));
    if (!display_) throw std::runtime_error("x11: cannot open display (is DISPLAY set?)");
    screen_ = DefaultScreen(display_.get());
    root_ = RootWindow(display_.get(), screen_);
  }

  bool grab(Frame& out) override {
    const int w = DisplayWidth(display_.get(), screen_);
    const int h = DisplayHeight(display_.get(), screen_);
    XImage* img = XGetImage(display_.get(), root_, 0, 0, w, h, AllPlanes, ZPixmap);
    if (!img) return false;

    out.width = static_cast<uint16_t>(w);
    out.height = static_cast<uint16_t>(h);
    out.bgra.resize(size_t(w) * h * 4);

    // Most X servers hand back 32bpp BGRX already (true on every modern
    // Linux desktop: Xorg/Xwayland with a 24/32-bit TrueColor visual), which
    // is exactly our wire format, so this is a straight copy. A server
    // using some other depth/byte order is not handled (yet): bail out
    // rather than show a garbled image.
    if (img->bits_per_pixel == 32 && img->depth >= 24 && img->byte_order == LSBFirst) {
      for (int y = 0; y < h; ++y)
        std::memcpy(&out.bgra[size_t(y) * w * 4], img->data + size_t(y) * img->bytes_per_line,
                    size_t(w) * 4);
      for (size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 255;  // alpha
      XDestroyImage(img);
      return true;
    }
    XDestroyImage(img);
    return false;
  }

private:
  std::unique_ptr<Display, XDisplayDeleter> display_;
  int screen_ = 0;
  Window root_ = 0;
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
