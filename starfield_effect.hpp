// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.

// Mixes (starIndex, variant, panel size) into a 32-bit hash. Stable across
// calls so a given segment always lays its stars in the same spots; varies
// with width/height so different panels produce different layouts.
uint32_t starfieldHash(uint16_t starIndex, uint8_t variant, uint16_t width, uint16_t height) {
  uint32_t h = 0x9E3779B9UL;
  h ^= uint32_t(starIndex) << 1;
  h *= 0x85EBCA6BUL;
  h ^= uint32_t(variant) << 8;
  h *= 0xC2B2AE35UL;
  h ^= uint32_t(width) << 5;
  h *= 0x27D4EB2FUL;
  h ^= uint32_t(height);
  h ^= h >> 16;
  h *= 0x85EBCA6BUL;
  h ^= h >> 13;
  return h;
}

uint16_t starfieldGreatestCommonDivisor(uint16_t a, uint16_t b) {
  while (b != 0) {
    const uint16_t remainder = uint16_t(a % b);
    a = b;
    b = remainder;
  }
  return a;
}

uint16_t starfieldCellStep(uint16_t cellCount) {
  if (cellCount <= 1) return 1;

  // A golden-ratio stride spreads the brightest early stars across the whole
  // segment. Adjust it to be coprime with the cell count so every cell is
  // visited exactly once before the sequence repeats.
  uint16_t step = max<uint16_t>(1, uint16_t(float(cellCount) * 0.6180339887f + 0.5f));
  while (starfieldGreatestCommonDivisor(step, cellCount) != 1) {
    step = uint16_t(step + 1U);
    if (step >= cellCount) step = 1;
  }
  return step;
}

void renderStarfieldGradient(Segment &seg, uint16_t width, uint16_t height) {
  // Vertical palette gradient — escape hatch for using the starfield seg as a
  // plain sky backdrop without bringing in a second effect.
  for (uint16_t y = 0; y < height; y++) {
    const uint8_t paletteIndex = (height <= 1) ? 0 : uint8_t((uint32_t(y) * 255U) / (height - 1U));
    const uint32_t color = seg.color_from_palette(paletteIndex, false, false, 0);
    for (uint16_t x = 0; x < width; x++) {
      seg.setPixelColorXY(x, y, color);
    }
  }
}

void renderStarfieldToSegment(Segment &seg) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  if (width == 0 || height == 0) return;

  // check1 = "Gradient" override — paints a static palette gradient instead of stars.
  if (seg.check1) {
    renderStarfieldGradient(seg, width, height);
    return;
  }

  seg.fill(BLACK);
  if (seg.intensity == 0) return;

  // The full star catalogue contains roughly one star per four pixels. Unlike
  // the old density implementation, its cells and coordinates never change as
  // intensity rises; intensity only controls which magnitude bands are visible.
  const uint32_t pixels = uint32_t(width) * height;
  const uint16_t maxStars = max<uint16_t>(1, uint16_t(pixels / 4U));

  // Split the full catalogue into roughly square cells and jitter one star
  // inside each selected cell. The coprime stride below permutes cell order so
  // the few high-magnitude dusk stars are distributed over the entire sky.
  const float idealColumns = sqrtf(float(maxStars) * float(width) / float(height));
  const uint16_t columns = min<uint16_t>(
    width,
    max<uint16_t>(1, uint16_t(ceilf(idealColumns)))
  );
  const uint16_t rows = uint16_t((uint32_t(maxStars) + columns - 1U) / columns);
  const uint16_t cellCount = uint16_t(columns * rows);
  const uint16_t cellStep = starfieldCellStep(cellCount);
  const uint16_t cellOffset = uint16_t(starfieldHash(0, 4, width, height) % cellCount);
  const float nightDepth = float(seg.intensity) / 255.0f;

  // Twinkle rate: sx=128 reproduces the legacy raster's 1.6 rad/s pulse, which
  // is the "spot on" feel the renderer originally landed on.
  const float rate = (float(seg.speed) / 128.0f) * STARFIELD_DEFAULT_RATE;
  const float timeS = float(strip.now) / 1000.0f;

  const uint32_t baseColor = SEGCOLOR(0);

  for (uint16_t i = 0; i < maxStars; i++) {
    const uint32_t hx = starfieldHash(i, 0, width, height);
    const uint32_t hy = starfieldHash(i, 1, width, height);
    const uint32_t hs = starfieldHash(i, 2, width, height);
    const uint32_t hm = starfieldHash(i, 3, width, height);

    // Each star owns one magnitude quantile, with deterministic jitter inside
    // it. Higher magnitude means brighter and visible earlier in the night.
    // A narrow reveal band lets adjacent magnitude bands fade in together;
    // compressing onset into 0..0.9 guarantees every star is fully revealed at
    // ix=255.
    const float magnitudeJitter = float(hm & 0xFFFFU) / 65536.0f;
    const float magnitudeRank = (i == 0)
      ? 0.0f
      : (float(i) + magnitudeJitter) / float(maxStars);
    const float revealOnset = magnitudeRank * (1.0f - STARFIELD_REVEAL_WIDTH);
    if (nightDepth <= revealOnset) break;
    float reveal = constrain(
      (nightDepth - revealOnset) / STARFIELD_REVEAL_WIDTH,
      0.0f,
      1.0f
    );
    reveal = reveal * reveal * (3.0f - 2.0f * reveal);

    const float magnitude = 1.0f - magnitudeRank;
    const float magnitudeAlpha = STARFIELD_MIN_MAGNITUDE_ALPHA
      + powf(magnitude, STARFIELD_MAGNITUDE_GAMMA) * STARFIELD_MAGNITUDE_ALPHA_SPAN;

    const uint16_t cellIndex = uint16_t(
      (uint32_t(cellOffset) + uint32_t(i) * cellStep) % cellCount
    );
    const uint16_t cellX = uint16_t(cellIndex % columns);
    const uint16_t cellY = uint16_t(cellIndex / columns);
    const uint16_t xStart = uint16_t((uint32_t(cellX) * width) / columns);
    const uint16_t xStop = uint16_t((uint32_t(cellX + 1U) * width) / columns);
    const uint16_t yStart = uint16_t((uint32_t(cellY) * height) / rows);
    const uint16_t yStop = uint16_t((uint32_t(cellY + 1U) * height) / rows);
    const uint16_t x = uint16_t(xStart + hx % (xStop - xStart));
    const uint16_t y = uint16_t(yStart + hy % (yStop - yStart));
    const float seed = float(hs & 0x3FF) / 1024.0f;

    // sin(time * rate + seed * 9) * 0.5 + 0.5 → 0..1, then re-mapped to 0.18..1.0
    // alpha so even the dimmest twinkle stays faintly visible (matches raster).
    const float twinkle = sinf(timeS * rate + seed * 9.0f) * 0.5f + 0.5f;
    const float fade = STARFIELD_MIN_FADE + twinkle * STARFIELD_FADE_SPAN;
    const uint8_t alpha = uint8_t(constrain(
      int16_t(reveal * magnitudeAlpha * fade * 255.0f + 0.5f),
      0,
      255
    ));

    if (seg.blendMode == 17) {
      seg.setPixelColorXY(x, y, RGBW32(R(baseColor), G(baseColor), B(baseColor), alpha));
    } else {
      seg.blendPixelColorXY(x, y, baseColor, alpha);
    }
  }
}
