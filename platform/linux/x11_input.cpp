// Remote input injection for X11 via XTest. Requires the XTEST extension,
// present on essentially every X server (Xorg, Xwayland).
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#include <cstdlib>
#include <stdexcept>

#include "vnc/capture.h"

namespace vnc {
namespace {

struct XDisplayDeleter { void operator()(Display* d) const { if (d) XCloseDisplay(d); } };

// Rough scancode-to-X11-keysym mapping would belong here; for now we treat
// `keycode` as an X11 hardware keycode directly, which is what a client
// running on X11 itself would naturally send.
class X11Input : public IInputSink {
public:
  X11Input() {
    display_.reset(XOpenDisplay(nullptr));
    if (!display_) throw std::runtime_error("x11: cannot open display");
    int ignore;
    if (!XTestQueryExtension(display_.get(), &ignore, &ignore, &ignore, &ignore))
      throw std::runtime_error("x11: XTEST extension not available");
  }

  void key(const KeyEvent& e) override {
    XTestFakeKeyEvent(display_.get(), static_cast<unsigned int>(e.keycode), e.down ? True : False,
                      0);
    XFlush(display_.get());
  }

  void pointer(const PointerEvent& e) override {
    XTestFakeMotionEvent(display_.get(), -1, e.x, e.y, 0);
    edge(1, e.buttons, Button1);
    edge(2, e.buttons, Button2);
    edge(4, e.buttons, Button3);
    if (e.wheel) {
      const unsigned int btn = e.wheel > 0 ? Button4 : Button5;
      for (int i = 0, n = std::abs(e.wheel); i < n; ++i) {
        XTestFakeButtonEvent(display_.get(), btn, True, 0);
        XTestFakeButtonEvent(display_.get(), btn, False, 0);
      }
    }
    lastButtons_ = e.buttons;
    XFlush(display_.get());
  }

private:
  void edge(uint8_t bit, uint8_t buttons, unsigned int x11Button) {
    const bool now = buttons & bit, was = lastButtons_ & bit;
    if (now != was) XTestFakeButtonEvent(display_.get(), x11Button, now ? True : False, 0);
  }

  std::unique_ptr<Display, XDisplayDeleter> display_;
  uint8_t lastButtons_ = 0;
};

}  // namespace

std::unique_ptr<IInputSink> makeScreenInput() {
  try {
    return std::make_unique<X11Input>();
  } catch (const std::exception&) {
    return nullptr;
  }
}

}  // namespace vnc
