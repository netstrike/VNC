#pragma once
#include <windows.h>

#include <string>

namespace vnc::win {

// Attaches the calling thread to whichever desktop currently receives
// input (the normal interactive desktop, or the Winlogon desktop shown at
// the login/lock screen, or the UAC secure desktop). This needs to run
// again before every capture or input call, because the input desktop can
// change at any moment (user logs in, locks the session, a UAC prompt
// appears). Returns the desktop's name, or an empty string if it could not
// be opened (for example, no SYSTEM privileges when a secure desktop is
// active).
inline std::wstring attachToInputDesktop() {
  HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
  if (!desk) return L"";
  std::wstring name;
  if (SetThreadDesktop(desk)) {
    wchar_t buf[128] = {};
    DWORD needed = 0;
    if (GetUserObjectInformationW(desk, UOI_NAME, buf, sizeof(buf), &needed)) name = buf;
  }
  CloseDesktop(desk);
  return name;
}

}  // namespace vnc::win
