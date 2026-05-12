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

  // Density: ix maps to 1..(pixels/16), matching the raster reference's ~6% coverage
  // at full slider. At default ix=160 a 32x10 segment renders ~13 stars.
  const uint32_t pixels = uint32_t(width) * height;
  const uint16_t maxStars = max<uint16_t>(1, uint16_t(pixels / 16U));
  const uint16_t starCount = uint16_t(1U + map(seg.intensity, 1, 255, 0, max<uint16_t>(0, maxStars - 1U)));

  // Twinkle rate: sx=128 reproduces the legacy raster's 1.6 rad/s pulse, which
  // is the "spot on" feel the renderer originally landed on.
  const float rate = (float(seg.speed) / 128.0f) * STARFIELD_DEFAULT_RATE;
  const float timeS = float(strip.now) / 1000.0f;

  const uint32_t baseColor = SEGCOLOR(0);

  for (uint16_t i = 0; i < starCount; i++) {
    const uint32_t hx = starfieldHash(i, 0, width, height);
    const uint32_t hy = starfieldHash(i, 1, width, height);
    const uint32_t hs = starfieldHash(i, 2, width, height);
    const uint16_t x = uint16_t(hx % width);
    const uint16_t y = uint16_t(hy % height);
    const float seed = float(hs & 0x3FF) / 1024.0f;

    // sin(time * rate + seed * 9) * 0.5 + 0.5 → 0..1, then re-mapped to 0.18..1.0
    // alpha so even the dimmest twinkle stays faintly visible (matches raster).
    const float twinkle = sinf(timeS * rate + seed * 9.0f) * 0.5f + 0.5f;
    const float fade = STARFIELD_MIN_FADE + twinkle * STARFIELD_FADE_SPAN;
    const uint8_t alpha = uint8_t(constrain(int16_t(fade * 255.0f + 0.5f), 0, 255));

    if (seg.blendMode == 17) {
      seg.setPixelColorXY(x, y, RGBW32(R(baseColor), G(baseColor), B(baseColor), alpha));
    } else {
      seg.blendPixelColorXY(x, y, baseColor, alpha);
    }
  }
}

