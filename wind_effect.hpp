// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.
//
// Horizontal counterpart to the precipitation effect: particles that blow
// across the panel instead of falling down it. Same fixed-point/parallax
// skeleton, but the dominant axis is X, gravity is replaced by a small
// tunable vertical drift, and each particle flutters on a sine so leaves
// tumble rather than slide. The scene owns the three particle colors, which
// is what makes the same effect read as summer grit, autumn leaves, or
// winter spindrift.
//
// Constants, particle state and the state accessor live here rather than in
// sprite_usermod_core.hpp so this file compiles on its own: test/wind_harness.cpp
// includes it against a stub Segment to step particle motion deterministically
// on a host, with no ESP32 and no panel.

constexpr uint32_t WIND_STATE_MAGIC = 0x574E4431UL;
constexpr uint16_t WIND_MAX_PARTICLES = 128;
constexpr uint8_t WIND_FP_SHIFT = 8;
constexpr int16_t WIND_FP_SCALE = 1 << WIND_FP_SHIFT;
// Flutter rate in phase units per second (256 units = one full cycle) at c1=0
// and the span added at c1=255. Leaves tumble faster than they travel, so even
// the slow end stays near one half-cycle per second.
constexpr uint8_t WIND_FLUTTER_RATE_MIN = 120;
constexpr uint8_t WIND_FLUTTER_RATE_SPAN = 135;
// Particles re-enter this many pixels outside the upwind edge, with sub-pixel
// jitter so they don't all cross the boundary on the same frame.
constexpr uint8_t WIND_SPAWN_MARGIN_PX = 2;

struct WindParticle {
  int32_t x;
  int32_t y;
  int16_t vx;
  int16_t vy;
  uint8_t phase;
  uint8_t seed;
  uint8_t state;
  uint8_t reserved;
} __attribute__((packed));

struct WindState {
  uint32_t magic;
  uint32_t lastUpdate;
  uint32_t prngState;
  uint16_t width;
  uint16_t height;
  uint16_t particleCount;
  uint16_t activeTarget;
};

inline WindParticle *getWindParticles(WindState *state) {
  return reinterpret_cast<WindParticle *>(state + 1);
}


uint16_t windRandom(WindState &state, uint16_t upperExclusive) {
  if (upperExclusive <= 1) return 0;
  state.prngState ^= state.prngState << 13;
  state.prngState ^= state.prngState >> 17;
  state.prngState ^= state.prngState << 5;
  return uint16_t(state.prngState % upperExclusive);
}

uint16_t getWindParticleCapacity(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) return 0;
  const uint32_t pixels = uint32_t(width) * height;
  // Sparser than rain: wind reads as scattered debris, not a curtain.
  const uint16_t areaLimit = max<uint16_t>(8, min<uint32_t>(WIND_MAX_PARTICLES, pixels / 6U + 4U));
  const size_t memoryLimit = (MAX_SEGMENT_DATA > sizeof(WindState))
    ? (MAX_SEGMENT_DATA - sizeof(WindState)) / sizeof(WindParticle)
    : 0;
  return min<uint16_t>(areaLimit, memoryLimit);
}

uint16_t getWindActiveTarget(const Segment &seg, uint16_t capacity) {
  if (capacity == 0) return 0;
  if (seg.intensity == 0) return 0;
  return map(seg.intensity, 1, 255, 1, capacity);
}

bool ensureWindState(Segment &seg, WindState *&state) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  const uint16_t capacity = getWindParticleCapacity(width, height);
  const size_t dataSize = sizeof(WindState) + size_t(capacity) * sizeof(WindParticle);

  state = nullptr;
  if (capacity == 0) return false;

  if (seg.data) {
    auto *existing = reinterpret_cast<WindState *>(seg.data);
    if (existing->magic == WIND_STATE_MAGIC && existing->width == width && existing->height == height && existing->particleCount == capacity) {
      state = existing;
      return true;
    }
  }

  seg.deallocateData();
  if (!seg.allocateData(dataSize)) return false;

  state = reinterpret_cast<WindState *>(seg.data);
  memset(state, 0, dataSize);
  state->magic = WIND_STATE_MAGIC;
  state->lastUpdate = strip.now;
  state->prngState = 0x1B873593UL ^ (uint32_t(width) << 16) ^ uint32_t(height) ^ strip.now;
  state->width = width;
  state->height = height;
  state->particleCount = capacity;
  return true;
}

// startUpwind=false scatters the particle anywhere in the segment (first fill,
// so the panel doesn't start empty and sweep in). startUpwind=true parks it
// just outside the edge the wind comes from, which is where every recycled
// particle re-enters.
// A particle is recycled once it is this far outside the segment. It must stay
// strictly larger than WIND_SPAWN_MARGIN_PX: if a freshly spawned particle
// lands outside the keep-alive window it is re-rolled on the very next frame,
// over and over, which reads on the panel as particles flickering between rows
// as they enter. The streak term keeps a particle alive while its trail is
// still inside the segment after the head has left.
inline int32_t windDespawnMargin(const Segment &seg) {
  return int32_t(WIND_SPAWN_MARGIN_PX + seg.custom3 + 1) * WIND_FP_SCALE;
}

void respawnWindParticle(WindState &state, WindParticle &particle, const Segment &seg, bool startUpwind) {
  const uint16_t width = max<uint16_t>(1, state.width);
  const uint16_t height = max<uint16_t>(1, state.height);
  const bool blowLeft = seg.check1;

  particle.state = 1;
  particle.seed = uint8_t(windRandom(state, 255));
  particle.phase = uint8_t(windRandom(state, 255));
  particle.y = int32_t(windRandom(state, height)) * WIND_FP_SCALE + windRandom(state, WIND_FP_SCALE);

  if (startUpwind) {
    // Between one and two pixels outside the upwind edge — inside the despawn
    // margin, so the particle survives to actually fly.
    const int32_t offset = int32_t(WIND_SPAWN_MARGIN_PX) * WIND_FP_SCALE - int32_t(windRandom(state, WIND_FP_SCALE));
    particle.x = blowLeft ? (int32_t(width) * WIND_FP_SCALE + offset) : -offset;
  } else {
    particle.x = int32_t(windRandom(state, width)) * WIND_FP_SCALE + windRandom(state, WIND_FP_SCALE);
  }

  particle.vx = 0;
  particle.vy = 0;
}

int16_t windBaseSpeed(const Segment &seg) {
  // sx=1 ≈ 3 px/sec (a barely-moving leaf), sx=128 ≈ 31 px/sec (roughly one
  // panel width per second), sx=255 ≈ 60 px/sec (debris).
  return seg.speed == 0 ? 0 : int16_t(map(seg.speed, 1, 255, 3, 60));
}

void updateWindMotion(WindParticle &particle, const Segment &seg) {
  // Parallax, same contract as precipitation: seed doubles as a depth value.
  // Background particles travel slower and render dimmer.
  const int16_t depthMul = int16_t(140 + (uint16_t(particle.seed) >> 1));  // 140..267
  const int16_t gust = int16_t((int32_t(windBaseSpeed(seg)) * depthMul) >> 8);
  particle.vx = seg.check1 ? int16_t(-gust) : gust;
  // c2=128 is neutral, above it particles settle, below it they lift. ±16 px/s
  // at the extremes, scaled by the same depth so near particles fall faster.
  const int16_t drift = int16_t(int16_t(seg.custom2) - 128) / 8;
  particle.vy = int16_t((int32_t(drift) * depthMul) >> 8);
}

// Flutter: a per-particle sine wobble on Y, in fixed-point subpixels. c1=128
// gives roughly ±1 px, c1=255 roughly ±2 px. Rate varies ±25% by seed so the
// field never pulses in lockstep.
inline int32_t windFlutterOffset(const WindParticle &particle, const Segment &seg) {
  if (seg.custom1 == 0) return 0;
  const int32_t amplitude = (int32_t(seg.custom1) * WIND_FP_SCALE) / 128;
  return ((int32_t(sin8_t(particle.phase)) - 128) * amplitude) / 128;
}

inline uint8_t windFlutterStep(const WindParticle &particle, const Segment &seg, uint16_t deltaMs) {
  if (seg.custom1 == 0 || deltaMs == 0) return 0;
  const uint16_t base = WIND_FLUTTER_RATE_MIN + uint16_t((uint16_t(seg.custom1) * WIND_FLUTTER_RATE_SPAN) / 255U);
  const uint16_t rate = uint16_t((uint32_t(base) * (192U + (uint16_t(particle.seed) >> 1))) >> 8);
  return uint8_t(min<uint32_t>(255U, (uint32_t(rate) * deltaMs) / 1000U));
}

// Depth → output alpha, matching the precipitation curve so the two layers
// look like they live in the same air.
inline uint8_t windDepthAlpha(uint8_t depth) {
  return uint8_t(80 + ((uint16_t(depth) * 175U) >> 8));  // 80..254
}

// Each particle owns one of the three segment colors, chosen by seed. A scene
// that only sets col[0] (the rest black) gets a uniform field rather than
// invisible particles.
inline uint32_t windParticleColor(const Segment &seg, uint8_t seed) {
  const uint32_t chosen = SEGCOLOR(seed % 3);
  return (R(chosen) | G(chosen) | B(chosen)) ? chosen : SEGCOLOR(0);
}

void drawWindPixel(Segment &seg, int16_t x, int16_t y, uint32_t color, uint8_t alpha) {
  if (alpha == 0 || (unsigned)x >= seg.vWidth() || (unsigned)y >= seg.vHeight()) return;
  // bm=17 (Alpha Over): hand the compositor a real alpha instead of blending
  // into black here. See drawPrecipitationPixel.
  if (seg.blendMode == 17) {
    seg.setPixelColorXY(x, y, RGBW32(R(color), G(color), B(color), alpha));
    return;
  }
  seg.blendPixelColorXY(x, y, color, alpha);
}

// Streaks trail straight upwind, unlike rain trails which follow the fall
// vector. Length 0 (the default) means bare particles, which is what leaves
// want; winter spindrift reads better with 2-3.
void drawWindStreak(Segment &seg, const WindParticle &particle, int16_t headX, int16_t headY,
                    uint32_t color, uint8_t depthAlpha) {
  const uint8_t streakLength = seg.custom3;
  if (streakLength == 0 || particle.vx == 0) return;
  const int16_t step = particle.vx < 0 ? 1 : -1;
  for (uint8_t index = 1; index <= streakLength; index++) {
    const uint8_t fade = uint8_t(255U - (uint16_t(index) * 255U) / (uint16_t(streakLength) + 1U));
    const uint8_t alpha = uint8_t((uint16_t(fade) * depthAlpha) / 255U);
    drawWindPixel(seg, int16_t(headX + step * int16_t(index)), headY, color, alpha);
  }
}

void drawWindParticle(Segment &seg, const WindParticle &particle) {
  const int16_t headX = int16_t(particle.x >> WIND_FP_SHIFT);
  const int16_t headY = int16_t((particle.y + windFlutterOffset(particle, seg)) >> WIND_FP_SHIFT);
  const uint8_t depthAlpha = windDepthAlpha(particle.seed);
  const uint32_t color = windParticleColor(seg, particle.seed);

  drawWindStreak(seg, particle, headX, headY, color, depthAlpha);
  drawWindPixel(seg, headX, headY, color, depthAlpha);
}

void updateWindParticle(WindState &state, WindParticle &particle, const Segment &seg, uint16_t index, uint16_t activeTarget, uint16_t deltaMs) {
  if (index >= activeTarget) {
    particle.state = 0;
    return;
  }

  if (particle.state == 0) {
    respawnWindParticle(state, particle, seg, false);
    updateWindMotion(particle, seg);
    return;
  }

  updateWindMotion(particle, seg);
  if (seg.speed == 0) return;

  particle.x += (int32_t(particle.vx) * deltaMs * WIND_FP_SCALE) / 1000L;
  particle.y += (int32_t(particle.vy) * deltaMs * WIND_FP_SCALE) / 1000L;
  particle.phase = uint8_t(particle.phase + windFlutterStep(particle, seg, deltaMs));

  // Recycle at the upwind edge once a particle leaves the segment on either axis.
  const int32_t margin = windDespawnMargin(seg);
  const int32_t maxX = int32_t(state.width) * WIND_FP_SCALE;
  const int32_t maxY = int32_t(state.height) * WIND_FP_SCALE;
  if (particle.x < -margin || particle.x > maxX + margin
      || particle.y < -margin || particle.y > maxY + margin) {
    respawnWindParticle(state, particle, seg, true);
  }
}

void renderWindToSegment(Segment &seg) {
  WindState *state = nullptr;
  if (!ensureWindState(seg, state) || !state) {
    seg.fill(BLACK);
    return;
  }

  const uint16_t deltaMs = min<uint32_t>(100, strip.now - state->lastUpdate);
  state->lastUpdate = strip.now;
  state->activeTarget = getWindActiveTarget(seg, state->particleCount);

  seg.fill(BLACK);
  WindParticle *particles = getWindParticles(state);
  for (uint16_t index = 0; index < state->particleCount; index++) {
    updateWindParticle(*state, particles[index], seg, index, state->activeTarget, deltaMs);
    if (particles[index].state != 0) drawWindParticle(seg, particles[index]);
  }
}
