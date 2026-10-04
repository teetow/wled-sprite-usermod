// Host harness for the cloud deck. Build and run through test/run-host-tests.sh.
#include "wled_stub.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../cloud_deck_effect.hpp"

static Segment makeDeck(uint8_t coverage, uint8_t thickness) {
  Segment seg(32, 10);
  seg.intensity = coverage;
  seg.custom1 = thickness;
  seg.colors[0] = RGBW32(200, 205, 215, 0);
  seg.colors[1] = RGBW32(90, 95, 105, 0);
  return seg;
}

static WindInputs breeze(float mps, bool left) {
  WindInputs wind{};
  wind.meanMps = wind.gustMps = mps;
  wind.blowLeft = left;
  wind.tuning = GustTuning{};
  return wind;
}

static uint8_t alphaAt(const Segment &seg, int x, int y) { return W(seg.getPixelColorXY(x, y)); }

// Share of pixels a cloud covers, averaged over a long stretch of drifting
// and reshaping, so one lucky frame cannot pass it.
static float measuredCoverage(uint8_t coverage) {
  Segment seg = makeDeck(coverage, 128);
  currentSegment = &seg;
  const WindInputs wind = breeze(6, false);
  strip.now = 1000;
  double covered = 0;
  unsigned samples = 0;
  for (unsigned frame = 0; frame < 3000; frame++) {
    strip.now += 100;
    renderCloudDeckToSegment(seg, &wind);
    if (frame % 25) continue;
    for (int y = 0; y < 10; y++) for (int x = 0; x < 32; x++) covered += alphaAt(seg, x, y) >= 96;
    samples += 320;
  }
  assert(seg.outOfBoundsWrites == 0);
  return float(covered / samples);
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  // Coverage is the share of sky under cloud, not an opacity.
  for (const float want : {0.2f, 0.5f, 0.8f}) {
    const float got = measuredCoverage(uint8_t(want * 255));
    printf("coverage %.0f%%: measured %.0f%%\n", want * 100, got * 100);
    assert(fabsf(got - want) < 0.12f);
  }

  // Clear sky draws nothing; a closed deck covers everything and keeps texture.
  Segment clear = makeDeck(0, 128);
  currentSegment = &clear;
  strip.now = 5000;
  renderCloudDeckToSegment(clear, nullptr);
  for (int y = 0; y < 10; y++) for (int x = 0; x < 32; x++) assert(alphaAt(clear, x, y) == 0);

  Segment closed = makeDeck(255, 128);
  currentSegment = &closed;
  renderCloudDeckToSegment(closed, nullptr);
  unsigned minAlpha = 255;
  double sum = 0, sumSq = 0;
  for (int y = 0; y < 10; y++) for (int x = 0; x < 32; x++) {
    minAlpha = std::min<unsigned>(minAlpha, alphaAt(closed, x, y));
    const double green = G(closed.getPixelColorXY(x, y));
    sum += green;
    sumSq += green * green;
  }
  const double spread = sqrt(sumSq / 320 - (sum / 320) * (sum / 320));
  printf("closed deck: faintest pixel alpha %u, color spread %.1f\n", minAlpha, spread);
  assert(minAlpha >= 150 && spread > 4);

  // A thin deck lets the sun show through its cores; a thick one hides it.
  auto strongest = [](uint8_t thickness) {
    Segment seg = makeDeck(255, thickness);
    currentSegment = &seg;
    strip.now = 9000;
    renderCloudDeckToSegment(seg, nullptr);
    unsigned best = 0;
    for (int y = 0; y < 10; y++) for (int x = 0; x < 32; x++) best = std::max<unsigned>(best, alphaAt(seg, x, y));
    return best;
  };
  const unsigned thin = strongest(0), thick = strongest(255);
  printf("densest pixel: thin deck %u, thick deck %u\n", thin, thick);
  assert(thin < 235 && thick >= 250);

  // The deck drifts the way the wind blows: the pattern a second later
  // matches this one shifted downwind.
  auto driftShift = [](bool left) {
    Segment seg = makeDeck(128, 128);
    currentSegment = &seg;
    const WindInputs wind = breeze(16, left); // 4 px/s
    strip.now = 20000;
    renderCloudDeckToSegment(seg, &wind);
    uint8_t before[10][32];
    for (int y = 0; y < 10; y++) for (int x = 0; x < 32; x++) before[y][x] = alphaAt(seg, x, y);
    for (int i = 0; i < 10; i++) {
      strip.now += 100;
      renderCloudDeckToSegment(seg, &wind);
    }
    int bestShift = 0;
    double bestError = 1e18;
    for (int shift = -8; shift <= 8; shift++) {
      double error = 0;
      for (int y = 0; y < 10; y++) for (int x = 8; x < 24; x++) {
        const double d = double(alphaAt(seg, x, y)) - before[y][x - shift];
        error += d * d;
      }
      if (error < bestError) { bestError = error; bestShift = shift; }
    }
    return bestShift;
  };
  const int right = driftShift(false), left = driftShift(true);
  printf("drift over 1 s at 16 m/s: right wind %+d px, left wind %+d px\n", right, left);
  assert(right >= 2 && right <= 6 && left <= -2 && left >= -6);

  // Same clock, same inputs, same sky.
  Segment a = makeDeck(160, 128), b = makeDeck(160, 128);
  strip.now = 40000;
  currentSegment = &a;
  renderCloudDeckToSegment(a, nullptr);
  currentSegment = &b;
  renderCloudDeckToSegment(b, nullptr);
  for (int y = 0; y < 10; y++) for (int x = 0; x < 32; x++) assert(a.getPixelColorXY(x, y) == b.getPixelColorXY(x, y));

  printf("Cloud deck covers what it is asked to, keeps texture, drifts downwind.\n");
  return 0;
}
