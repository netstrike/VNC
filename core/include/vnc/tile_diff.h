#pragma once
#include <cstdint>
#include <vector>

#include "vnc/protocol.h"

namespace vnc {

struct Frame {
  uint16_t width = 0, height = 0;
  std::vector<uint8_t> bgra;  // width*height*4, tightly packed
};

// Compares two frames tile by tile and returns the changed tiles as Raw
// rects. If `prev` has a different size (or is empty) the whole frame is sent.
std::vector<Rect> diffTiles(const Frame& prev, const Frame& cur, int tile = 64);

// Applies rects to a frame (resizing it to width x height first if needed).
void applyRects(Frame& frame, uint16_t width, uint16_t height,
                const std::vector<Rect>& rects);

}  // namespace vnc
