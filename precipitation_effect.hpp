// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.

uint16_t precipitationRandom(PrecipitationState &state, uint16_t upperExclusive) {
  if (upperExclusive <= 1) return 0;
  state.prngState ^= state.prngState << 13;
  state.prngState ^= state.prngState >> 17;
  state.prngState ^= state.prngState << 5;
  return uint16_t(state.prngState % upperExclusive);
}

uint16_t getPrecipitationParticleCapacity(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) return 0;
  const uint32_t pixels = uint32_t(width) * height;
  const uint16_t areaLimit = max<uint16_t>(8, min<uint32_t>(PRECIPITATION_MAX_PARTICLES, pixels / 3U + 6U));
  const size_t memoryLimit = (MAX_SEGMENT_DATA > sizeof(PrecipitationState))
    ? (MAX_SEGMENT_DATA - sizeof(PrecipitationState)) / sizeof(PrecipitationParticle)
    : 0;
  return min<uint16_t>(areaLimit, memoryLimit);
}

uint16_t getPrecipitationActiveTarget(const Segment &seg, uint16_t capacity) {
  if (capacity == 0) return 0;
  if (seg.intensity == 0) return 0;
  return map(seg.intensity, 1, 255, 1, capacity);
}

bool ensurePrecipitationState(Segment &seg, PrecipitationState *&state) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  const uint16_t capacity = getPrecipitationParticleCapacity(width, height);
  const size_t dataSize = sizeof(PrecipitationState) + size_t(capacity) * sizeof(PrecipitationParticle);

  state = nullptr;
  if (capacity == 0) return false;

  if (seg.data) {
    auto *existing = reinterpret_cast<PrecipitationState *>(seg.data);
    if (existing->magic == PRECIPITATION_STATE_MAGIC && existing->width == width && existing->height == height && existing->particleCount == capacity) {
      state = existing;
      return true;
    }
  }

  seg.deallocateData();
  if (!seg.allocateData(dataSize)) return false;

  state = reinterpret_cast<PrecipitationState *>(seg.data);
  memset(state, 0, dataSize);
  state->magic = PRECIPITATION_STATE_MAGIC;
  state->lastUpdate = strip.now;
  state->prngState = 0x9E3779B9UL ^ (uint32_t(width) << 16) ^ uint32_t(height) ^ strip.now;
  state->width = width;
  state->height = height;
  state->particleCount = capacity;
  return true;
}

void respawnPrecipitationParticle(PrecipitationState &state, PrecipitationParticle &particle, const Segment &seg, bool startAbove) {
  const uint16_t width = max<uint16_t>(1, state.width);

  particle.state = 1;
  particle.age = 0;
  particle.ttl = 0;
  particle.seed = precipitationRandom(state, 255);
  particle.x = int32_t(precipitationRandom(state, width) * PRECIPITATION_FP_SCALE + precipitationRandom(state, PRECIPITATION_FP_SCALE));
  particle.y = startAbove
    ? -int32_t(precipitationRandom(state, max<uint16_t>(1, state.height)) * PRECIPITATION_FP_SCALE)
    : int32_t(precipitationRandom(state, max<uint16_t>(1, state.height)) * PRECIPITATION_FP_SCALE);
  particle.vx = 0;
  particle.vy = 0;
}

int16_t precipitationBaseFallSpeed(const Segment &seg) {
  // Slider range tuned for matrix scenes: sx=1 ≈ 4 px/sec (gentle drizzle),
  // sx=128 ≈ 34 px/sec (normal rain), sx=255 ≈ 64 px/sec (downpour).
  return seg.speed == 0 ? 0 : int16_t(map(seg.speed, 1, 255, 4, 64));
}

uint8_t precipitationSplashAgeStep(const Segment &seg, uint16_t deltaMs) {
  const int16_t baseFall = precipitationBaseFallSpeed(seg);
  if (baseFall <= 0 || deltaMs == 0) return 0;

  const uint32_t numerator = uint32_t(deltaMs) * uint32_t(baseFall) * PRECIPITATION_SPLASH_TIME_SCALE;
  const uint32_t denominator =
      uint32_t(PRECIPITATION_SPLASH_FRAME_MS) * PRECIPITATION_REFERENCE_FALL * PRECIPITATION_SPLASH_RATE_DIVIDER;
  const uint32_t step = max<uint32_t>(1U, (numerator + denominator / 2U) / denominator);
  return uint8_t(min<uint32_t>(255U, step));
}

void updatePrecipitationMotion(PrecipitationParticle &particle, const Segment &seg) {
  const int16_t baseFall = precipitationBaseFallSpeed(seg);
  // Parallax: each particle's seed acts as a depth value (0..255 ≈ background
  // ..foreground). Background drops fall slower (~55%) and render dimmer; full
  // speed/brightness at depth=255. This is what gives the rain a layered feel
  // instead of a flat wall of uniform particles.
  const int16_t depthMul = int16_t(140 + (uint16_t(particle.seed) >> 1));  // 140..267
  const int16_t fallSpeed = int16_t((int32_t(baseFall) * depthMul) >> 8);
  const int16_t shear = int16_t(seg.custom2) - 128;
  particle.vx = int16_t((int32_t)shear * max<int16_t>(8, fallSpeed) / 128);
  particle.vy = fallSpeed;
}

// Parallax-modulated despawn row. Background drops (low seed = far away)
// land near the horizon (upper part of the seg); foreground drops land at
// the bottom edge. Splash impact uses the same Y, so the depth cue carries
// through the whole drop lifecycle.
uint16_t precipitationDespawnY(uint8_t depth, uint16_t height) {
  if (height < 2) return height ? height - 1 : 0;
  const uint16_t topY = (uint32_t(height) * 5U) / 8U; // ~62.5% from the top
  const uint16_t spanY = (height - 1) - topY;
  return topY + uint16_t((uint32_t(depth) * spanY) >> 8);
}

void startPrecipitationSplash(PrecipitationState &state, PrecipitationParticle &particle, const Segment &seg) {
  const uint8_t bounciness = seg.custom1;
  if (bounciness == 0) {
    respawnPrecipitationParticle(state, particle, seg, true);
    return;
  }

  particle.state = 2;
  particle.age = 0;
  // 6..21 normal-speed frames at ~30fps = 200..700ms. Splash age is stored in
  // fixed-point frame units, then advanced by the same sx time scale as falling.
  particle.ttl = uint8_t((6 + (bounciness >> 4)) * PRECIPITATION_SPLASH_TIME_SCALE);
  // Snap to the parallax-correct row so the burst origin sits on an integer pixel.
  particle.y = int32_t(precipitationDespawnY(particle.seed, state.height)) * PRECIPITATION_FP_SCALE;
}

void drawPrecipitationPixel(Segment &seg, int16_t x, int16_t y, uint32_t color, uint8_t alpha) {
  if (alpha == 0 || (unsigned)x >= seg.vWidth() || (unsigned)y >= seg.vHeight()) return;
  // bm=17 (Alpha Over): write the source color with W=alpha and let WLED's
  // segment compositor do the over-blend cleanly. Other blend modes go through
  // the standard add-style blend.
  if (seg.blendMode == 17) {
    seg.setPixelColorXY(x, y, RGBW32(R(color), G(color), B(color), alpha));
    return;
  }
  seg.blendPixelColorXY(x, y, color, alpha);
}

// Parallax depth → output alpha. Background drops (low depth) ≈ 30% bright,
// foreground at full 100%. Keeps the rain from looking like a flat curtain.
inline uint8_t precipitationDepthAlpha(uint8_t depth) {
  return uint8_t(80 + ((uint16_t(depth) * 175U) >> 8));  // 80..254
}

// Trails fade from SEGCOLOR(1) toward SEGCOLOR(2) along their length. With
// alpha-over compositing (bm=17), a "transparent" tail is best achieved by
// fading alpha, not by writing black RGB (which darkens the background). So
// when SEGCOLOR(2) is solid black we treat it as "fade alpha only" and keep
// the RGB at SEGCOLOR(1); otherwise the user is explicitly asking for a
// color-blend tail and we honor it.
void drawPrecipitationTrail(Segment &seg, const PrecipitationParticle &particle,
                            int16_t headX, int16_t headY, uint8_t trailLength,
                            uint8_t depthAlpha, uint8_t fadeMul) {
  if (trailLength == 0 || particle.vy == 0) return;
  const int16_t denom = max<int16_t>(1, abs(particle.vy));
  const uint32_t col1 = SEGCOLOR(1);
  const uint32_t col2 = SEGCOLOR(2);
  const bool col2IsBlack = (R(col2) | G(col2) | B(col2)) == 0;
  for (uint8_t index = trailLength; index > 0; index--) {
    const int16_t trailX = headX - int16_t((int32_t(particle.vx) * index) / denom);
    const int16_t trailY = headY - index;
    const uint8_t blend = (uint16_t(index) * 255U) / trailLength;
    const uint32_t trailColor = col2IsBlack ? col1 : color_blend(col1, col2, blend);
    // Three alpha multipliers stacked: head-to-tail (255-blend), splash decay
    // (fadeMul = 255 while falling, ramps to 0 over the splash window), and
    // parallax depth.
    const uint32_t a = (uint32_t(255 - blend) * fadeMul * depthAlpha) / (255U * 255U);
    drawPrecipitationPixel(seg, trailX, trailY, trailColor, uint8_t(a));
  }
}

void drawPrecipitationParticle(Segment &seg, const PrecipitationParticle &particle) {
  const int16_t headX = particle.x >> PRECIPITATION_FP_SHIFT;
  const int16_t headY = particle.y >> PRECIPITATION_FP_SHIFT;
  const uint8_t depthAlpha = precipitationDepthAlpha(particle.seed);
  const uint8_t trailLength = seg.custom3;

  if (particle.state == 2) {
    // 2D splash. Three deterministic gravity-arc droplets per impact, with a
    // few coherence rules:
    //   - Splash gravity scales with the particle's fall speed: heavy rain
    //     produces snappy splashes, drizzle splashes hang in the air.
    //   - c1 (Bounciness) sets the initial upward velocity. c1=255 still caps
    //     peak height around 2-3 pixels — at higher bounciness the visual reads
    //     as "hail".
    //   - Per-droplet seed jitter so droplets don't all jump in lockstep.
    //   - Horizontal spread fixed (no slider) — the old c1=Splash radius slot
    //     is now bounciness, since "splash radius" wasn't pulling its weight.
    const uint8_t ttl = particle.ttl ? particle.ttl : PRECIPITATION_SPLASH_TIME_SCALE;
    const uint8_t age = min<uint8_t>(particle.age, ttl);
    const uint8_t progress = (uint16_t(age) * 255U) / ttl;
    const uint8_t fade = 255 - progress;

    const uint8_t bounciness = seg.custom1;
    // Both the initial upward velocity (vy0) and gravity scale with the
    // drop's actual fall speed (which itself carries parallax — foreground
    // drops fall faster than background ones, so they also splash harder).
    // Bounciness adds a base kick; sx-driven speedKick adds proportional
    // energy. Cap vy0 at 3 so the splash stays compact against the ground line,
    // and let gravity scale up too so peak height stays bounded.
    const int16_t baseVy0   = max<int16_t>(1, int16_t(bounciness) / 128);  // 1..1 (2 at 255)
    const int16_t speedKick = max<int16_t>(0, (abs(particle.vy) - 16) / 32); // 0..1 (1 at vy≥48)
    const int16_t vy0Max    = min<int16_t>(3, baseVy0 + speedKick);
    // Splash gravity tied to fall speed for coherence. Default vy≈34 → g=2;
    // downpour vy=64 → g=4 (snappy); drizzle vy=4 → g=1 (hangs).
    const int16_t splashG   = max<int16_t>(1, abs(particle.vy) / 16);

    static const int8_t SPLASH_VX[3] = { -1, 0, 1 };

    for (uint8_t i = 0; i < 3; i++) {
      const int16_t t = int16_t(age) + PRECIPITATION_SPLASH_TIME_SCALE;
      // Per-droplet vy0 with seed-driven variance. The middle droplet (i=1)
      // gets the +1 boost so it visibly peaks higher than the outer two.
      const uint8_t mix = uint8_t((particle.seed * 17U + i * 53U) & 0xFFU);
      const int16_t vy0 = -(vy0Max + (i == 1 ? 1 : 0) - int16_t(mix & 0x01));
      const int16_t dx = int16_t((int32_t(SPLASH_VX[i]) * t) / PRECIPITATION_SPLASH_TIME_SCALE);
      const int32_t dyFixed = int32_t(vy0) * t
        + (int32_t(splashG) * t * (t - PRECIPITATION_SPLASH_TIME_SCALE))
          / (2 * PRECIPITATION_SPLASH_TIME_SCALE);
      const int16_t dy = int16_t(dyFixed / PRECIPITATION_SPLASH_TIME_SCALE);
      const int16_t px = headX + dx;
      const int16_t py = headY + dy;
      const uint8_t alpha = uint8_t((uint16_t(fade) * depthAlpha) / 255);
      drawPrecipitationPixel(seg, px, py, SEGCOLOR(0), alpha);
    }

    // Keep the trail alive during the splash window, fading with splash age,
    // so the drop doesn't visually pop out of existence at the moment of impact.
    drawPrecipitationTrail(seg, particle, headX, headY, trailLength, depthAlpha, fade);
    return;
  }

  drawPrecipitationTrail(seg, particle, headX, headY, trailLength, depthAlpha, 255);
  drawPrecipitationPixel(seg, headX, headY, SEGCOLOR(0), depthAlpha);
}

void updatePrecipitationParticle(PrecipitationState &state, PrecipitationParticle &particle, const Segment &seg, uint16_t index, uint16_t activeTarget, uint16_t deltaMs) {
  if (index >= activeTarget) {
    particle.state = 0;
    return;
  }

  if (particle.state == 0) {
    respawnPrecipitationParticle(state, particle, seg, false);
    return;
  }

  if (particle.state == 2) {
    const uint8_t ageStep = precipitationSplashAgeStep(seg, deltaMs);
    if (ageStep == 0) return;
    particle.age = (particle.age > 255U - ageStep) ? 255 : particle.age + ageStep;
    if (particle.age >= particle.ttl) respawnPrecipitationParticle(state, particle, seg, true);
    return;
  }

  updatePrecipitationMotion(particle, seg);
  if (seg.speed == 0) return;

  const int32_t moveX = (int32_t(particle.vx) * deltaMs * PRECIPITATION_FP_SCALE) / 1000L;
  const int32_t moveY = (int32_t(particle.vy) * deltaMs * PRECIPITATION_FP_SCALE) / 1000L;
  particle.x += moveX;
  particle.y += moveY;

  const int32_t maxX = int32_t(state.width) * PRECIPITATION_FP_SCALE;
  while (particle.x < 0) particle.x += maxX;
  while (particle.x >= maxX) particle.x -= maxX;

  const uint16_t despawnY = precipitationDespawnY(particle.seed, state.height);
  if (particle.y >= int32_t(despawnY) * PRECIPITATION_FP_SCALE) startPrecipitationSplash(state, particle, seg);
}

void renderPrecipitationToSegment(Segment &seg) {
  PrecipitationState *state = nullptr;
  if (!ensurePrecipitationState(seg, state) || !state) {
    seg.fill(BLACK);
    return;
  }

  uint16_t deltaMs = min<uint32_t>(100, strip.now - state->lastUpdate);
  state->lastUpdate = strip.now;
  state->activeTarget = getPrecipitationActiveTarget(seg, state->particleCount);

  seg.fill(BLACK);
  PrecipitationParticle *particles = getPrecipitationParticles(state);
  for (uint16_t index = 0; index < state->particleCount; index++) {
    updatePrecipitationParticle(*state, particles[index], seg, index, state->activeTarget, deltaMs);
    if (particles[index].state != 0) drawPrecipitationParticle(seg, particles[index]);
  }
}

