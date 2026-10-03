// Fallback used only on platforms with neither a Windows nor a Linux/X11
// capture backend (platform/windows, platform/linux). Kept out of vnc_core
// itself so that on Windows/Linux the real implementation is the only one
// the linker ever sees.
#include "vnc/capture.h"

namespace vnc {

std::unique_ptr<ICaptureSource> makeScreenSource() { return nullptr; }
std::unique_ptr<IInputSink> makeScreenInput() { return nullptr; }

}  // namespace vnc
