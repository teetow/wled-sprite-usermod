// Minimal stand-in for the slice of WLED that the particle effects touch, so
// effect sources can be compiled and stepped on a host.
//
// The point is determinism: the harness owns the clock (`strip.now`), and the
// effects seed their PRNG from it, so a given start time replays exactly the
// same field every run. Nothing here talks to hardware, and no wall clock is
// ever read.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

// ── Arduino/FastLED-shaped helpers the effects call ──────────────────────────

template <typename T>
static inline T min(T a, T b) { return a < b ? a : b; }
template <typename T>
static inline T max(T a, T b) { return a > b ? a : b; }

static inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

static inline uint32_t RGBW32(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return (uint32_t(w) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}
static inline uint8_t R(uint32_t c) { return uint8_t((c >> 16) & 0xFF); }
static inline uint8_t G(uint32_t c) { return uint8_t((c >> 8) & 0xFF); }
static inline uint8_t B(uint32_t c) { return uint8_t(c & 0xFF); }
static inline uint8_t W(uint32_t c) { return uint8_t((c >> 24) & 0xFF); }

constexpr uint32_t BLACK = 0;
// esp32dev without PSRAM, from FX.h. Kept identical so particle capacity math
// resolves the same way here as on the panel.
constexpr size_t MAX_SEGMENT_DATA = 64 * 1024;

// Verbatim from wled00/wled_math.cpp (Bhaskara I sine, @dedehai). Copied rather
// than approximated so flutter offsets match the firmware bit for bit.
static inline int16_t sin16_t(uint16_t theta) {
  int scale = 1;
  if (theta > 0x7FFF) {
    theta = 0xFFFF - theta;
    scale = -1;
  }
  uint32_t precal = theta * (0x7FFF - theta);
  uint64_t numerator = (uint64_t)precal * (4 * 0x7FFF);
  int32_t denominator = 1342095361 - precal;
  int16_t result = numerator / denominator;
  return result * scale;
}

static inline uint8_t sin8_t(uint8_t theta) {
  int32_t sin16 = sin16_t((uint16_t)theta * 257);
  sin16 += 0x7FFF + 128;
  return min(sin16, int32_t(0xFFFF)) >> 8;
}

static inline uint32_t color_blend(uint32_t a, uint32_t b, uint8_t blend) {
  const uint8_t inv = 255 - blend;
  return RGBW32(
    uint8_t((R(a) * inv + R(b) * blend) / 255),
    uint8_t((G(a) * inv + G(b) * blend) / 255),
    uint8_t((B(a) * inv + B(b) * blend) / 255),
    uint8_t((W(a) * inv + W(b) * blend) / 255)
  );
}

// ── Segment ──────────────────────────────────────────────────────────────────

class Segment {
 public:
  Segment(uint16_t width, uint16_t height) : _width(width), _height(height), _pixels(size_t(width) * height, 0) {}
  ~Segment() { deallocateData(); }

  uint16_t vWidth() const { return _width; }
  uint16_t vHeight() const { return _height; }

  uint8_t speed = 128;
  uint8_t intensity = 128;
  uint8_t custom1 = 0;
  uint8_t custom2 = 128;
  uint8_t custom3 = 0;
  bool check1 = false;
  uint8_t blendMode = 17;  // alpha-over, which is what the panel scene uses
  uint32_t colors[3] = {0, 0, 0};
  uint8_t *data = nullptr;

  bool allocateData(size_t len) {
    deallocateData();
    data = static_cast<uint8_t *>(calloc(1, len));
    _dataSize = data ? len : 0;
    return data != nullptr;
  }
  void deallocateData() {
    free(data);
    data = nullptr;
    _dataSize = 0;
  }
  size_t dataSize() const { return _dataSize; }

  void fill(uint32_t color) {
    for (auto &pixel : _pixels) pixel = color;
  }

  void setPixelColorXY(int x, int y, uint32_t color) {
    if (!inBounds(x, y)) { outOfBoundsWrites++; return; }
    _pixels[size_t(y) * _width + x] = color;
  }

  void blendPixelColorXY(uint16_t x, uint16_t y, uint32_t color, uint8_t blend) {
    if (!inBounds(int(x), int(y))) { outOfBoundsWrites++; return; }
    uint32_t &pixel = _pixels[size_t(y) * _width + x];
    pixel = color_blend(pixel, color, blend);
  }

  uint32_t pixel(int x, int y) const {
    return inBounds(x, y) ? _pixels[size_t(y) * _width + x] : 0;
  }

  // Under alpha-over the effect writes color+alpha and lets the compositor
  // blend, so "lit" means a non-zero alpha channel.
  bool lit(int x, int y) const { return W(pixel(x, y)) != 0; }

  unsigned outOfBoundsWrites = 0;

 private:
  bool inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < int(_width) && y < int(_height);
  }

  uint16_t _width;
  uint16_t _height;
  size_t _dataSize = 0;
  std::vector<uint32_t> _pixels;
};

// SEGCOLOR reads the segment currently being rendered; the harness points it at
// whichever segment it is stepping.
static Segment *currentSegment = nullptr;
#define SEGCOLOR(x) (currentSegment->colors[(x)])

struct StripStub {
  uint32_t now = 0;
  bool isMatrix = true;
};
static StripStub strip;
