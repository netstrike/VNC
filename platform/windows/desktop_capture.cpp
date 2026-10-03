// Screen capture backend for Windows: DXGI Desktop Duplication for the
// common single-monitor case, with a GDI BitBlt path used whenever a
// specific subset of monitors is selected (or DXGI is unavailable). The
// caller already requires an authenticated, TLS-encrypted connection (see
// vnc/auth.h and vnc/tls_transport.h) before any frame reaches the network;
// this file only produces the pixels.
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "desktop_util.h"
#include "vnc/capture.h"

namespace vnc {
namespace {

template <class T>
struct ComPtr {
  T* p = nullptr;
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ~ComPtr() { reset(); }
  void reset() { if (p) { p->Release(); p = nullptr; } }
  T** operator&() { reset(); return &p; }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};

struct Box { int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0; };

BOOL CALLBACK monitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM data) {
  auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(data);
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(hMon, &info)) return TRUE;
  MonitorInfo m;
  m.index = static_cast<int>(out->size());
  m.x = info.rcMonitor.left;
  m.y = info.rcMonitor.top;
  m.width = static_cast<uint16_t>(info.rcMonitor.right - info.rcMonitor.left);
  m.height = static_cast<uint16_t>(info.rcMonitor.bottom - info.rcMonitor.top);
  m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  int len = WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, nullptr, 0, nullptr, nullptr);
  if (len > 0) {
    std::string name(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, name.data(), len, nullptr, nullptr);
    m.name = name;
  }
  out->push_back(m);
  return TRUE;
}

std::vector<MonitorInfo> enumMonitors() {
  std::vector<MonitorInfo> out;
  EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&out));
  return out;
}

Box boundingBoxFor(const std::vector<MonitorInfo>& monitors, const std::vector<int>& indices) {
  if (monitors.empty()) {
    return Box{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
               GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
               GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
  }
  const bool all = indices.empty();
  Box b{INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
  bool any = false;
  for (const auto& m : monitors) {
    bool wanted = all || std::find(indices.begin(), indices.end(), m.index) != indices.end();
    if (!wanted) continue;
    any = true;
    b.x1 = std::min(b.x1, m.x);
    b.y1 = std::min(b.y1, m.y);
    b.x2 = std::max(b.x2, int32_t(m.x + m.width));
    b.y2 = std::max(b.y2, int32_t(m.y + m.height));
  }
  if (!any) return boundingBoxFor(monitors, {});  // no valid index selected: fall back to all
  return b;
}

class DesktopCapture : public ICaptureSource {
public:
  std::vector<MonitorInfo> listMonitors() override { return enumMonitors(); }
  void selectMonitors(const std::vector<int>& indices) override { selected_ = indices; }

  bool grab(Frame& out) override {
    // Re-attach every call: the input desktop can change between grabs.
    std::wstring desk = win::attachToInputDesktop();
    if (desk.empty()) return false;
    if (desk != desktop_) {
      desktop_ = desk;
      resetDxgi();
      dxgiFailed_ = false;
    }

    auto monitors = enumMonitors();
    const bool wantsEverything = selected_.empty() || selected_.size() >= monitors.size();

    // Fast path: a single physical monitor and nothing excluded from it.
    // Multi-monitor composition (several selected, or several present with
    // none excluded) goes through the slower but simpler GDI path below.
    if (wantsEverything && monitors.size() <= 1 && !dxgiFailed_ && grabDxgi()) {
      out = last_;
      return true;
    }

    Box box = boundingBoxFor(monitors, selected_);
    if (grabGdi(box)) { out = last_; return true; }
    return false;
  }

private:
  bool initDxgi() {
    if (dup_) return true;
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev_, &fl, &ctx_)))
      return false;
    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIOutput> output;
    ComPtr<IDXGIOutput1> output1;
    if (FAILED(dev_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev))))
      return false;
    if (FAILED(dxgiDev->GetAdapter(&adapter))) return false;
    if (FAILED(adapter->EnumOutputs(0, &output))) return false;
    if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1))))
      return false;
    if (FAILED(output1->DuplicateOutput(dev_.p, &dup_))) return false;
    return true;
  }

  void resetDxgi() {
    staging_.reset();
    dup_.reset();
    ctx_.reset();
    dev_.reset();
  }

  bool grabDxgi() {
    if (!initDxgi()) { dxgiFailed_ = true; return false; }
    DXGI_OUTDUPL_FRAME_INFO info;
    ComPtr<IDXGIResource> res;
    HRESULT hr = dup_->AcquireNextFrame(16, &info, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return !last_.bgra.empty();  // nothing changed
    if (FAILED(hr)) { resetDxgi(); return false; }  // e.g. ACCESS_LOST: retry next call

    ComPtr<ID3D11Texture2D> tex;
    bool ok = SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)));
    if (ok) ok = copyToStaging(tex.p);
    res.reset();
    tex.reset();
    dup_->ReleaseFrame();
    return ok;
  }

  bool copyToStaging(ID3D11Texture2D* tex) {
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);
    if (!staging_ || stagingW_ != desc.Width || stagingH_ != desc.Height) {
      staging_.reset();
      desc.Usage = D3D11_USAGE_STAGING;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.MiscFlags = 0;
      if (FAILED(dev_->CreateTexture2D(&desc, nullptr, &staging_))) return false;
      stagingW_ = desc.Width;
      stagingH_ = desc.Height;
    }
    ctx_->CopyResource(staging_.p, tex);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(staging_.p, 0, D3D11_MAP_READ, 0, &m))) return false;
    last_.width = static_cast<uint16_t>(stagingW_);
    last_.height = static_cast<uint16_t>(stagingH_);
    last_.bgra.resize(size_t(stagingW_) * stagingH_ * 4);
    for (UINT y = 0; y < stagingH_; ++y)
      std::memcpy(&last_.bgra[size_t(y) * stagingW_ * 4],
                  static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch,
                  size_t(stagingW_) * 4);
    ctx_->Unmap(staging_.p, 0);
    return true;
  }

  // Captures exactly the virtual-desktop rectangle `box` (which may be the
  // full virtual screen, a single monitor, or the union of several selected
  // monitors) via BitBlt.
  bool grabGdi(const Box& box) {
    const int w = box.x2 - box.x1, h = box.y2 - box.y1;
    if (w <= 0 || h <= 0) return false;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down, matches our wire format
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bmp) {
      HGDIOBJ old = SelectObject(mem, bmp);
      if (BitBlt(mem, 0, 0, w, h, screen, box.x1, box.y1, SRCCOPY | CAPTUREBLT)) {
        last_.width = static_cast<uint16_t>(w);
        last_.height = static_cast<uint16_t>(h);
        last_.bgra.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + size_t(w) * h * 4);
        for (size_t i = 3; i < last_.bgra.size(); i += 4) last_.bgra[i] = 255;
        ok = true;
      }
      SelectObject(mem, old);
      DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
  }

  ComPtr<ID3D11Device> dev_;
  ComPtr<ID3D11DeviceContext> ctx_;
  ComPtr<IDXGIOutputDuplication> dup_;
  ComPtr<ID3D11Texture2D> staging_;
  UINT stagingW_ = 0, stagingH_ = 0;
  bool dxgiFailed_ = false;
  std::wstring desktop_;
  std::vector<int> selected_;
  Frame last_;
};

}  // namespace

std::unique_ptr<ICaptureSource> makeScreenSource() { return std::make_unique<DesktopCapture>(); }

}  // namespace vnc
