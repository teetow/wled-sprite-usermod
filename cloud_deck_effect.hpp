// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.
//
// The sky's cloud layer: a soft, procedural deck that sits in front of the
// sun, moon and stars and behind the cloud sprites. Coverage opens or closes
// its gaps, so the sun is hidden by cloud rather than faded out; thickness
// decides how much of it shows through thin patches. The deck drifts with the
// panel wind, slower than the sprites because it is higher and farther away,
// and reshapes slowly even in still air.
//
// Segment controls: Intensity is coverage (0 clear, 255 closed), Custom 1
// thickness, Color 1 the thin, lit cloud and Color 2 the thick core. Speed and
// Check 1 are drift and direction for a segment without panel_wind.
//
// Compiles on its own: test/cloud_deck_harness.cpp steps it against a stub
// Segment on a host.

#include "panel_wind.hpp"

constexpr uint32_t CLOUD_DECK_MAGIC = 0x434c4431UL;
// Feature size in LEDs: wide, flat banks.
constexpr float CLOUD_DECK_SCALE_X = 11.0f;
constexpr float CLOUD_DECK_SCALE_Y = 3.5f;
// Toward the horizon the layer is seen edge-on: features flatten and there is
// more cloud in the line of sight.
constexpr float CLOUD_DECK_HORIZON_FLATTEN = 0.5f;
constexpr float CLOUD_DECK_HORIZON_BIAS = 0.06f;
// Seconds per unit of reshaping, and on-panel drift per m/s of wind.
constexpr float CLOUD_DECK_MORPH_S = 25.0f;
constexpr float CLOUD_DECK_DRIFT_PER_MPS = 0.25f;
// Soft edge width, and the least depth from edge to densest core, in noise units.
constexpr float CLOUD_DECK_EDGE = 0.06f;
constexpr float CLOUD_DECK_DEPTH = 0.25f;
// The noise lattice repeats this many cells along x and in time, so drift and
// reshaping wrap seamlessly instead of losing float precision.
constexpr int CLOUD_DECK_PERIOD = 4096;
constexpr unsigned CLOUD_DECK_BINS = 256;

struct CloudDeckState {
  uint32_t magic, lastUpdate;
  float offsetX, morph;
};

inline float cloudDeckLattice(int x, int y, int z) {
  x &= CLOUD_DECK_PERIOD - 1;
  z &= CLOUD_DECK_PERIOD - 1;
  const uint32_t key = uint32_t(x) * 0x9E3779B1U ^ uint32_t(y) * 0x85EBCA77U ^ uint32_t(z) * 0xC2B2AE3DU;
  return (gustHash(key ^ 0x5bd1e995U) >> 8) / 16777215.0f;
}

inline float cloudDeckValueNoise(float x, float y, float z) {
  const int ix = int(floorf(x)), iy = int(floorf(y)), iz = int(floorf(z));
  const auto smooth = [](float t) { return t * t * (3 - 2 * t); };
  const float fx = smooth(x - ix), fy = smooth(y - iy), fz = smooth(z - iz);
  const auto plane = [&](int z0) {
    const float top = cloudDeckLattice(ix, iy, z0) * (1 - fx) + cloudDeckLattice(ix + 1, iy, z0) * fx;
    const float bottom = cloudDeckLattice(ix, iy + 1, z0) * (1 - fx) + cloudDeckLattice(ix + 1, iy + 1, z0) * fx;
    return top * (1 - fy) + bottom * fy;
  };
  return plane(iz) * (1 - fz) + plane(iz + 1) * fz;
}

// Two octaves in lattice units; 0..1.
inline float cloudDeckField(float x, float y, float z) {
  return 0.65f * cloudDeckValueNoise(x, y, z)
    + 0.35f * cloudDeckValueNoise(x * 2.0f + 17.0f, y * 2.0f + 5.0f, z * 2.0f + 9.0f);
}

// The field's distribution, measured once, so coverage maps to the share of
// sky actually covered: quantiles[i] is the value i/BINS of the field lies below.
static float cloudDeckQuantiles[CLOUD_DECK_BINS + 1];
static bool cloudDeckQuantilesReady = false;

inline void measureCloudDeck() {
  if (cloudDeckQuantilesReady) return;
  uint16_t counts[CLOUD_DECK_BINS] = {};
  unsigned total = 0;
  for (int z = 0; z < 8; z++) for (int y = 0; y < 16; y++) for (int x = 0; x < 32; x++) {
    const float value = cloudDeckField(x * 0.37f, y * 0.41f, z * 0.53f);
    counts[min(CLOUD_DECK_BINS - 1, unsigned(value * CLOUD_DECK_BINS))]++;
    total++;
  }
  unsigned seen = 0, bin = 0;
  for (unsigned q = 0; q <= CLOUD_DECK_BINS; q++) {
    const unsigned wanted = total * q / CLOUD_DECK_BINS;
    while (bin < CLOUD_DECK_BINS && seen + counts[bin] < wanted) seen += counts[bin++];
    cloudDeckQuantiles[q] = float(bin) / CLOUD_DECK_BINS;
  }
  cloudDeckQuantilesReady = true;
}

// The field value above which a pixel is cloud. Full coverage still leaves the
// lowest points a soft edge's width inside the cloud, so a closed deck keeps
// its texture instead of turning flat.
inline float cloudDeckThreshold(float coverage) {
  measureCloudDeck();
  if (coverage <= 0) return 2.0f;
  const float floorValue = cloudDeckQuantiles[0] - CLOUD_DECK_EDGE;
  if (coverage >= 1) return floorValue;
  const float position = (1 - coverage) * CLOUD_DECK_BINS;
  const unsigned index = min(CLOUD_DECK_BINS - 1, unsigned(position));
  const float t = position - index;
  const float value = cloudDeckQuantiles[index] * (1 - t) + cloudDeckQuantiles[index + 1] * t;
  return max(floorValue, value);
}

// `wind` is the panel wind when the host has sent it; null drifts at Speed/32
// px/s in Check 1's direction.
void renderCloudDeckToSegment(Segment &seg, const WindInputs *wind = nullptr) {
  const uint16_t width = seg.vWidth(), height = seg.vHeight();
  seg.fill(BLACK);
  if (!width || !height) return;
  if (seg.dataSize() != sizeof(CloudDeckState) && !seg.allocateData(sizeof(CloudDeckState))) return;
  auto &state = *reinterpret_cast<CloudDeckState *>(seg.data);
  if (state.magic != CLOUD_DECK_MAGIC) {
    state = {CLOUD_DECK_MAGIC, strip.now, 0, 0};
  }
  const float seconds = min<uint32_t>(100, strip.now - state.lastUpdate) / 1000.0f;
  state.lastUpdate = strip.now;

  const float meanMps = wind ? wind->meanMps : 0;
  const bool blowLeft = wind ? wind->blowLeft : seg.check1;
  const float drift = wind ? meanMps * CLOUD_DECK_DRIFT_PER_MPS : seg.speed / 32.0f;
  const float period = CLOUD_DECK_SCALE_X * CLOUD_DECK_PERIOD;
  state.offsetX = fmodf(state.offsetX + (blowLeft ? -drift : drift) * seconds + period, period);
  state.morph = fmodf(state.morph + seconds / CLOUD_DECK_MORPH_S * (1 + 0.1f * meanMps), float(CLOUD_DECK_PERIOD));

  const float threshold = cloudDeckThreshold(seg.intensity / 255.0f);
  if (threshold > 1.5f) return;
  // Depth runs from the cloud's edge to the densest point the field reaches,
  // so a closed deck shades from thin to thick instead of saturating flat.
  const float depthSpan = max(CLOUD_DECK_DEPTH, cloudDeckQuantiles[CLOUD_DECK_BINS] - threshold);
  const float thickness = seg.custom1 / 255.0f;
  const float density = 1 + 5 * thickness;
  const uint32_t lit = SEGCOLOR(0), core = SEGCOLOR(1);
  for (uint16_t y = 0; y < height; y++) {
    const float down = height > 1 ? float(y) / (height - 1) : 0;
    const float scaleY = CLOUD_DECK_SCALE_Y * (1 - CLOUD_DECK_HORIZON_FLATTEN * down);
    const float bias = CLOUD_DECK_HORIZON_BIAS * down;
    for (uint16_t x = 0; x < width; x++) {
      const float sampleX = (x + seg.start - state.offsetX + period) / CLOUD_DECK_SCALE_X;
      const float inside = cloudDeckField(sampleX, (y + seg.startY) / scaleY, state.morph) + bias - threshold;
      if (inside <= 0) continue;
      const float edge = min(1.0f, inside / CLOUD_DECK_EDGE);
      const float depth = min(1.0f, inside / depthSpan);
      const float opacity = edge * edge * (3 - 2 * edge) * (1 - expf(-density * (0.3f + depth)));
      const uint32_t color = color_blend(lit, core, uint8_t(255 * depth * (0.4f + 0.6f * thickness)));
      const uint8_t alpha = uint8_t(255 * opacity);
      if (!alpha) continue;
      if (seg.blendMode == 17) seg.setPixelColorXY(x, y, RGBW32(R(color), G(color), B(color), alpha));
      else seg.blendPixelColorXY(x, y, color, alpha);
    }
  }
}
