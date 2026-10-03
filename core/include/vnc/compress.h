#pragma once
#include "vnc/protocol.h"

namespace vnc {

// Lossless compression of a rect's pixel data with Zstd, at a low level
// chosen for speed over ratio (see compress.cpp): screen content compresses
// well even at level 1, and encoding time directly adds to end-to-end
// latency. Mutates `r` in place:
//   - compressRect: if the compressed form is smaller than the raw pixels,
//     replaces r.data with it and sets r.encoding = Encoding::Zstd;
//     otherwise leaves r unchanged (still Encoding::Raw) rather than ship a
//     bigger payload for incompressible content (e.g. a photo or noise).
//   - decompressRect: if r.encoding == Encoding::Zstd, inflates r.data back
//     to w*h*4 raw bytes and sets r.encoding = Encoding::Raw; a no-op
//     otherwise. Throws std::runtime_error if the compressed data is
//     malformed or doesn't inflate to exactly w*h*4 bytes.
void compressRect(Rect& r);
void decompressRect(Rect& r);

}  // namespace vnc
