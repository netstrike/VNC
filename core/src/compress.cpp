#include "vnc/compress.h"

#include <stdexcept>

#include <zstd.h>

namespace vnc {

// Level 1: the fastest Zstd offers. Screen tiles are small (at most 64x64x4
// = 16KB) and sent every frame, so encode time matters more than squeezing
// out a few extra percent of ratio; level 1 is already very effective on
// typical UI content (flat fills, text, repeated patterns).
constexpr int kZstdLevel = 1;

void compressRect(Rect& r) {
  if (r.encoding != Encoding::Raw || r.data.empty()) return;
  const size_t bound = ZSTD_compressBound(r.data.size());
  std::vector<uint8_t> out(bound);
  const size_t n = ZSTD_compress(out.data(), bound, r.data.data(), r.data.size(), kZstdLevel);
  if (ZSTD_isError(n) || n >= r.data.size()) return;  // not worth it: keep Raw
  out.resize(n);
  r.data = std::move(out);
  r.encoding = Encoding::Zstd;
}

void decompressRect(Rect& r) {
  if (r.encoding != Encoding::Zstd) return;
  const size_t expected = size_t(r.w) * r.h * 4;
  std::vector<uint8_t> out(expected);
  const size_t n = ZSTD_decompress(out.data(), expected, r.data.data(), r.data.size());
  if (ZSTD_isError(n) || n != expected)
    throw std::runtime_error("compress: malformed Zstd rect data");
  r.data = std::move(out);
  r.encoding = Encoding::Raw;
}

}  // namespace vnc
