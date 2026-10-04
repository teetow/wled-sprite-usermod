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

#include "panel_wind.hpp"
#include "leaf_exchange.hpp"

constexpr uint32_t WIND_STATE_MAGIC = 0x574E4436UL;
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
  uint8_t landing; // picks the ground row where this leaf may settle as litter
  float motionX, motionY;
};
// Naturally aligned with no padding; packing would only force byte-wise
// access to the float members.
static_assert(sizeof(WindParticle) == 24, "WindParticle layout changed");

struct WindState {
  uint32_t magic;
  uint32_t lastUpdate;
  uint32_t prngState;
  bool timedSpawning;
  uint32_t spawnProgress;
  uint16_t spawnThreshold;
  uint16_t width;
  uint16_t height;
  uint16_t particleCount;
  uint16_t activeTarget;
  WindInputs wind; // this frame's wind; the air field itself is panel-wide
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
  memset(seg.data, 0, dataSize);
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

void updateWindMotion(WindParticle &particle, const Segment &seg, const WindInputs &wind);

void respawnWindParticle(WindState &state, WindParticle &particle, const Segment &seg, bool startUpwind) {
  const uint16_t width = max<uint16_t>(1, state.width);
  const uint16_t height = max<uint16_t>(1, state.height);
  const bool blowLeft = state.wind.blowLeft;

  particle.state = 1;
  particle.seed = uint8_t(windRandom(state, 255));
  particle.phase = uint8_t(windRandom(state, 255));
  particle.landing = uint8_t(windRandom(state, 255));

  // Choose the most open of a few random positions. In rate mode, births still
  // enter just outside the upwind edge, but avoid stacking on leaves waiting
  // there for a gust. Initial population mode spreads across both dimensions.
  int32_t bestX = 0, bestY = 0;
  int64_t bestDistance = -1;
  const WindParticle *others = getWindParticles(&state);
  for (unsigned trial = 0; trial < 12; trial++) {
    const int32_t y = int32_t(windRandom(state, height)) * WIND_FP_SCALE
                      + windRandom(state, WIND_FP_SCALE);
    int32_t x;
    if (startUpwind) {
      // One to two pixels outside the edge, inside the despawn margin.
      const int32_t offset = int32_t(WIND_SPAWN_MARGIN_PX) * WIND_FP_SCALE
                             - int32_t(windRandom(state, WIND_FP_SCALE));
      x = blowLeft ? int32_t(width) * WIND_FP_SCALE + offset : -offset;
    } else {
      x = int32_t(windRandom(state, width)) * WIND_FP_SCALE
          + windRandom(state, WIND_FP_SCALE);
    }
    int64_t nearest = 0x7fffffffffffffffLL;
    for (uint16_t i = 0; i < state.particleCount; i++) {
      const WindParticle &other = others[i];
      if (!other.state || &other == &particle) continue;
      const int64_t dx = (int64_t(x) - other.x) / WIND_FP_SCALE;
      const int64_t dy = (int64_t(y) - other.y) / WIND_FP_SCALE;
      nearest = min(nearest, dx * dx + dy * dy);
    }
    if (nearest > bestDistance) {
      bestDistance = nearest;
      bestX = x;
      bestY = y;
    }
    if (nearest == 0x7fffffffffffffffLL) break;
  }
  particle.x = bestX;
  particle.y = bestY;

  updateWindMotion(particle, seg, state.wind);
  particle.motionX = seg.check2 ? (blowLeft ? -1 : 1) * windBaseSpeed(state.wind.airMean) : particle.vx;
  particle.motionY = seg.check2 ? 0 : particle.vy;
}

void updateWindMotion(WindParticle &particle, const Segment &seg, const WindInputs &wind) {
  // Parallax, same contract as precipitation: seed doubles as a depth value.
  // Background particles travel slower and render dimmer.
  const int16_t depthMul = int16_t(140 + (uint16_t(particle.seed) >> 1));  // 140..267
  const float mean = windBaseSpeed(wind.airMean);
  const float ceiling = windBaseSpeed(wind.airGust);
  const float x = float(particle.x) / WIND_FP_SCALE + seg.start;
  const float pulse = ceiling > mean
    ? gustFieldStrength(x, strip.now, wind.blowLeft, wind.activity, wind.tuning) : 0.0f;
  const int16_t speed = int16_t((mean + (ceiling - mean) * pulse) * depthMul / 256.0f);
  particle.vx = wind.blowLeft ? int16_t(-speed) : speed;
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
  const int16_t headY = int16_t((particle.y + (seg.check2 ? 0 : windFlutterOffset(particle, seg))) >> WIND_FP_SHIFT);
  const uint8_t depthAlpha = windDepthAlpha(particle.seed);
  const uint32_t color = windParticleColor(seg, particle.seed);

  drawWindStreak(seg, particle, headX, headY, color, depthAlpha);
  drawWindPixel(seg, headX, headY, color, depthAlpha);
}

// While ground Leaves runs, an airborne leaf coming down over the ground band
// settles as litter once it reaches its own landing row, spreading landings
// across depth. It lands only on an LED clear of existing litter and only
// while the litter has room; otherwise it flies on and tries again.
bool settleWindLeaf(WindParticle &particle, const Segment &seg) {
  LeafExchange &exchange = leafExchange;
  if (!leafExchangeAlive(exchange.groundSeen, exchange.groundSeenAt) || exchange.groundRoom == 0) return false;
  const float vy = seg.check2 ? particle.motionY : particle.vy;
  if (vy <= 0) return false;
  const int rows = exchange.groundBottom - exchange.groundTop;
  if (rows <= 0) return false;
  const float x = float(particle.x) / WIND_FP_SCALE + seg.start;
  const float y = float(particle.y) / WIND_FP_SCALE + seg.startY;
  const int column = int(floorf(x + 0.5f)), row = int(floorf(y + 0.5f));
  if (row < exchange.groundTop + particle.landing % rows || !groundLandingFree(column, row)) return false;
  const float vx = seg.check2 ? particle.motionX : particle.vx;
  if (!postLeafHandoff(exchange.toGround, column, row, vx, 0, particle.seed % 3)) return false;
  exchange.groundRoom--;
  exchange.landingsOffered++;
  if (column >= 0 && column < int(LEAF_EXCHANGE_MAX_COLUMNS) && row < int(LEAF_EXCHANGE_MAX_ROWS))
    exchange.groundLit[row] |= uint64_t(1) << column; // no second landing beside it this frame
  particle.state = 0;
  return true;
}

void updateWindParticle(WindState &state, WindParticle &particle, const Segment &seg, uint16_t index, uint16_t activeTarget, uint16_t deltaMs) {
  if (index >= activeTarget) {
    particle.state = 0;
    return;
  }

  if (particle.state == 0) {
    if (state.timedSpawning) return;
    respawnWindParticle(state, particle, seg, false);
    return;
  }

  const WindInputs &wind = state.wind;
  if (!seg.check2 && wind.airGust == 0) return;
  // Existing particles respond to the shared front at their current column.
  if (seg.check2) {
    const float absoluteX = float(particle.x) / WIND_FP_SCALE + seg.start;
    const float absoluteY = float(particle.y) / WIND_FP_SCALE + seg.startY;
    const float fx = airGridX(absoluteX), fy = airGridY(absoluteY);
    const float depth = (140 + (uint16_t(particle.seed) >> 1)) / 256.0f;
    const float pulse = gustFieldStrength(absoluteX, strip.now, wind.blowLeft, wind.activity, wind.tuning);
    const float responseMs = pulse > 0 ? wind.tuning.responseMs : wind.tuning.settleMs;
    const float coupling = min(1.0f, deltaMs / responseMs * (0.8f + particle.seed / 640.0f));
    const float kick = (windBaseSpeed(wind.airGust) - windBaseSpeed(wind.airMean)) * pulse;
    const float airX = flowSample(airField.flow.u, fx, fy) + (wind.blowLeft ? -kick : kick);
    particle.motionX += (airX * depth - particle.motionX) * coupling;
    const float drift = (int(seg.custom2) - 128) / 8.0f
      * flowClamp((windBaseSpeed(wind.airMean) + kick) / 12.0f, 0, 1);
    particle.motionY += ((flowSample(airField.flow.v, fx, fy) + drift) * depth - particle.motionY) * coupling;
    particle.vx = int16_t(particle.motionX);
    particle.vy = int16_t(particle.motionY);
  } else updateWindMotion(particle, seg, wind);

  if (seg.check2) {
    particle.x += int32_t(particle.motionX * deltaMs * WIND_FP_SCALE / 1000.0f);
    particle.y += int32_t(particle.motionY * deltaMs * WIND_FP_SCALE / 1000.0f);
  } else {
    particle.x += (int32_t(particle.vx) * deltaMs * WIND_FP_SCALE) / 1000L;
    particle.y += (int32_t(particle.vy) * deltaMs * WIND_FP_SCALE) / 1000L;
  }
  particle.phase = uint8_t(particle.phase + windFlutterStep(particle, seg, deltaMs));

  if (settleWindLeaf(particle, seg)) return;

  // Recycle at the upwind edge once a particle leaves the segment on either axis.
  const int32_t margin = windDespawnMargin(seg);
  const int32_t maxX = int32_t(state.width) * WIND_FP_SCALE;
  const int32_t maxY = int32_t(state.height) * WIND_FP_SCALE;
  if (particle.x < -margin || particle.x > maxX + margin
      || particle.y < -margin || particle.y > maxY + margin) {
    if (state.timedSpawning) particle.state = 0;
    else respawnWindParticle(state, particle, seg, true);
  }
}

// One emission budget for the whole segment, independent of travel speed and
// recycling. Threshold jitter of +/-30% keeps a trickle from looking clockwork.
// Budget units are milliseconds * tenths-of-a-particle/second: 10000 = one birth.
void spawnTimedWind(WindState &state, const Segment &seg, int16_t rateTenths, uint16_t deltaMs) {
  if (rateTenths <= 0 || state.wind.airGust == 0 || state.activeTarget == 0) {
    state.spawnProgress = 0;
    return;
  }
  if (state.spawnThreshold == 0) state.spawnThreshold = 7000U + windRandom(state, 6001);
  state.spawnProgress += uint32_t(deltaMs) * min<int16_t>(30, rateTenths);
  if (state.spawnProgress < state.spawnThreshold) return;
  state.spawnProgress -= state.spawnThreshold;
  state.spawnThreshold = 7000U + windRandom(state, 6001);
  WindParticle *particles = getWindParticles(&state);
  for (uint16_t i = 0; i < state.activeTarget; i++) {
    if (particles[i].state != 0) continue;
    respawnWindParticle(state, particles[i], seg, true);
    return;
  }
  // If the pool is full, drop this birth; never accumulate a catch-up burst.
}

// A ground leaf lifted clear of the horizon continues here as an airborne
// particle at the same LED, with its own palette color and near-full opacity.
void adoptLiftedLeaves(WindState &state, const Segment &seg) {
  WindParticle *particles = getWindParticles(&state);
  for (LeafHandoff &handoff : leafExchange.toAir) {
    const bool live = leafHandoffLive(handoff);
    handoff.pending = false;
    if (!live) continue;
    for (uint16_t i = 0; i < state.activeTarget; i++) {
      WindParticle &particle = particles[i];
      if (particle.state != 0) continue;
      particle = {};
      particle.x = int32_t((handoff.x - seg.start) * WIND_FP_SCALE);
      particle.y = int32_t((handoff.y - seg.startY) * WIND_FP_SCALE);
      particle.seed = uint8_t(249 + handoff.color % 3); // seed % 3 picks the color; 249..251 is near-opaque
      particle.phase = uint8_t(windRandom(state, 255));
      particle.landing = uint8_t(windRandom(state, 255));
      particle.motionX = handoff.vx;
      particle.motionY = handoff.vy;
      particle.vx = int16_t(handoff.vx);
      particle.vy = int16_t(handoff.vy);
      particle.state = 1;
      leafExchange.liftsTaken++;
      break;
    }
  }
}

// `wind` is panelWindInputs() once the host has sent panel_wind, otherwise
// windInputsFromSegment(seg).
void renderWindToSegment(Segment &seg, const WindInputs &wind, int16_t spawnRateTenths = -1) {
  WindState *state = nullptr;
  if (!ensureWindState(seg, state) || !state) {
    seg.fill(BLACK);
    return;
  }

  const uint16_t deltaMs = min<uint32_t>(100, strip.now - state->lastUpdate);
  state->lastUpdate = strip.now;
  const bool timed = spawnRateTenths >= 0;
  if (state->timedSpawning != timed) {
    state->spawnProgress = 0;
    state->spawnThreshold = 0;
  }
  state->timedSpawning = timed;
  state->wind = wind;
  state->activeTarget = timed && seg.intensity > 0
    ? state->particleCount : getWindActiveTarget(seg, state->particleCount);

  if (seg.check2) advancePanelAir(wind);
  adoptLiftedLeaves(*state, seg);
  seg.fill(BLACK);
  WindParticle *particles = getWindParticles(state);
  for (uint16_t index = 0; index < state->particleCount; index++) {
    updateWindParticle(*state, particles[index], seg, index, state->activeTarget, deltaMs);
  }
  if (timed) spawnTimedWind(*state, seg, spawnRateTenths, deltaMs);
  unsigned room = 0;
  for (uint16_t index = 0; index < state->particleCount; index++) {
    if (particles[index].state != 0) drawWindParticle(seg, particles[index]);
    else room += index < state->activeTarget;
  }
  leafExchange.airSeen = true;
  leafExchange.airSeenAt = strip.now;
  leafExchange.airRoom = uint8_t(min(255U, room));
}
