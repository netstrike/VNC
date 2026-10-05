#include "vnc/tile_diff.h"

#include <algorithm>
#include <cstring>

#include "vnc/compress.h"

namespace vnc {

std::vector<Rect> diffTiles(const Frame& prev, const Frame& cur, int tile) {
  std::vector<Rect> out;
  const bool full = prev.width != cur.width || prev.height != cur.height ||
                    prev.bgra.size() != cur.bgra.size();
  const size_t stride = size_t(cur.width) * 4;

  for (int ty = 0; ty < cur.height; ty += tile) {
    for (int tx = 0; tx < cur.width; tx += tile) {
      const int w = std::min<int>(tile, cur.width - tx);
      const int h = std::min<int>(tile, cur.height - ty);
      bool changed = full;
      for (int row = 0; row < h && !changed; ++row) {
        const size_t off = size_t(ty + row) * stride + size_t(tx) * 4;
        changed = std::memcmp(prev.bgra.data() + off, cur.bgra.data() + off, size_t(w) * 4) != 0;
      }
      if (!changed) continue;
      Rect r;
      r.x = uint16_t(tx); r.y = uint16_t(ty); r.w = uint16_t(w); r.h = uint16_t(h);
      r.encoding = Encoding::Raw;
      r.data.resize(size_t(w) * h * 4);
      for (int row = 0; row < h; ++row) {
        const size_t off = size_t(ty + row) * stride + size_t(tx) * 4;
        std::memcpy(r.data.data() + size_t(row) * w * 4, cur.bgra.data() + off, size_t(w) * 4);
      }
      out.push_back(std::move(r));
    }
  }
  return out;
}

void applyRects(Frame& frame, uint16_t width, uint16_t height, std::vector<Rect>& rects) {
  if (frame.width != width || frame.height != height) {
    frame.width = width;
    frame.height = height;
    frame.bgra.assign(size_t(width) * height * 4, 0);
  }
  const size_t stride = size_t(width) * 4;
  for (Rect& r : rects) {
    if (r.encoding != Encoding::Raw) decompressRect(r);
    if (r.encoding != Encoding::Raw) continue;  // unknown encoding: skip it
    if (size_t(r.x) + r.w > width || size_t(r.y) + r.h > height) continue;
    if (r.data.size() != size_t(r.w) * r.h * 4) continue;
    for (int row = 0; row < r.h; ++row) {
      std::memcpy(frame.bgra.data() + size_t(r.y + row) * stride + size_t(r.x) * 4,
                  r.data.data() + size_t(row) * r.w * 4, size_t(r.w) * 4);
    }
  }
}

}  // namespace vnc
