#include "vnc/capture.h"

namespace vnc {
namespace {

class PatternSource : public ICaptureSource {
public:
  PatternSource(uint16_t w, uint16_t h) : w_(w), h_(h) {}
  bool grab(Frame& f) override {
    f.width = w_;
    f.height = h_;
    f.bgra.resize(size_t(w_) * h_ * 4);
    const int bx = int(t_ * 4 % (w_ - 40));
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        uint8_t* p = &f.bgra[(size_t(y) * w_ + x) * 4];
        const bool box = x >= bx && x < bx + 40 && y >= 20 && y < 60;
        p[0] = box ? 255 : uint8_t(x * 255 / w_);
        p[1] = box ? 255 : uint8_t(y * 255 / h_);
        p[2] = box ? 255 : 64;
        p[3] = 255;
      }
    }
    ++t_;
    return true;
  }

private:
  uint16_t w_, h_;
  unsigned t_ = 0;
};

class NullInput : public IInputSink {
public:
  void key(const KeyEvent&) override {}
  void pointer(const PointerEvent&) override {}
};

}  // namespace

std::unique_ptr<ICaptureSource> makePatternSource(uint16_t w, uint16_t h) {
  return std::make_unique<PatternSource>(w, h);
}
std::unique_ptr<IInputSink> makeNullInput() { return std::make_unique<NullInput>(); }

}  // namespace vnc
