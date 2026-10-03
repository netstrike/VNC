// Screen capture backend for Windows: DXGI Desktop Duplication, with a GDI
// BitBlt fallback for the cases DXGI does not cover (older hardware, or a
// desktop where DXGI duplication is not available). The caller already
// requires an authenticated, TLS-encrypted connection (see vnc/auth.h and
// vnc/tls_transport.h) before any frame reaches the network; this file only
// produces the pixels.
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

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

class DesktopCapture : public ICaptureSource {
public:
  bool grab(Frame& out) override {
    // Re-attach every call: the input desktop can change between grabs.
    std::wstring desk = win::attachToInputDesktop();
    if (desk.empty()) return false;
    if (desk != desktop_) {
      desktop_ = desk;
      resetDxgi();
      dxgiFailed_ = false;
    }

    if (!dxgiFailed_ && grabDxgi()) { out = last_; return true; }
    if (grabGdi()) { out = last_; return true; }
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
    if (FAILED(adapter->EnumOutputs(0, &output))) return false;  // primary output for now
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

  bool grabGdi() {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
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
      if (BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT)) {
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
  Frame last_;
};

}  // namespace

std::unique_ptr<ICaptureSource> makeScreenSource() { return std::make_unique<DesktopCapture>(); }

}  // namespace vnc
