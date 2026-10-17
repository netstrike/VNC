// Remote input injection for Windows, via SendInput on whichever desktop
// currently receives input (see desktop_util.h). Reached only after the
// TLS + password handshake in vnc/auth.h has succeeded for this connection.
#include <windows.h>

#include "desktop_util.h"
#include "vnc/capture.h"

namespace vnc {
namespace {

class WinInput : public IInputSink {
public:
  void key(const KeyEvent& e) override {
    win::attachToInputDesktop();
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(e.keycode & 0xff);
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (e.keycode & 0x100 ? KEYEVENTF_EXTENDEDKEY : 0) |
                    (e.down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(in));
  }

  void pointer(const PointerEvent& e) override {
    win::attachToInputDesktop();
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return;

    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = static_cast<LONG>((e.x - vx) * 65535LL / (vw > 1 ? vw - 1 : 1));
    in.mi.dy = static_cast<LONG>((e.y - vy) * 65535LL / (vh > 1 ? vh - 1 : 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));

    sendButtonEdge(1, e.buttons, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
    sendButtonEdge(2, e.buttons, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);
    sendButtonEdge(4, e.buttons, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
    lastButtons_ = e.buttons;

    if (e.wheel) {
      INPUT w = {};
      w.type = INPUT_MOUSE;
      w.mi.dwFlags = MOUSEEVENTF_WHEEL;
      w.mi.mouseData = static_cast<DWORD>(static_cast<int>(e.wheel));
      SendInput(1, &w, sizeof(w));
    }
  }

private:
  void sendButtonEdge(uint8_t bit, uint8_t buttons, DWORD downFlag, DWORD upFlag) {
    const bool now = buttons & bit, was = lastButtons_ & bit;
    if (now == was) return;
    INPUT b = {};
    b.type = INPUT_MOUSE;
    b.mi.dwFlags = now ? downFlag : upFlag;
    SendInput(1, &b, sizeof(b));
  }

  uint8_t lastButtons_ = 0;
};

}  // namespace

std::unique_ptr<IInputSink> makeScreenInput() { return std::make_unique<WinInput>(); }

}  // namespace vnc
