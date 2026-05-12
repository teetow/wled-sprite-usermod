// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.

uint8_t ditheredGradientThreshold(uint16_t x, uint16_t y) {
  static const uint8_t bayer4x4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
  };
  return bayer4x4[((y & 0x03) << 2) | (x & 0x03)];
}

uint8_t getDitheredGradientPaletteIndex(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t dither, uint8_t rotation) {
  if (width <= 1 && height <= 1) return 0;

  const float angle = (float(rotation) / 255.0f) * DITHERED_GRADIENT_TWO_PI;
  const float axisX = sinf(angle);
  const float axisY = cosf(angle);
  const float maxX = (width > 1) ? float(width - 1) : 0.0f;
  const float maxY = (height > 1) ? float(height - 1) : 0.0f;
  const float p0 = 0.0f;
  const float p1 = axisX * maxX;
  const float p2 = axisY * maxY;
  const float p3 = p1 + p2;
  const float minProjection = min(min(p0, p1), min(p2, p3));
  const float maxProjection = max(max(p0, p1), max(p2, p3));
  const float range = maxProjection - minProjection;
  if (range <= 0.001f) return 0;

  float paletteIndex = ((axisX * x + axisY * y - minProjection) * 255.0f) / range;
  if (dither > 0) {
    const float ordered = (float(ditheredGradientThreshold(x, y)) - 7.5f) / 15.0f;
    paletteIndex += ordered * ((float(dither) * 96.0f) / 255.0f);
  }

  return uint8_t(constrain(int16_t(lroundf(paletteIndex)), 0, 255));
}

void renderDitheredGradientToSegment(Segment &seg) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  if (width == 0 || height == 0) return;

  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      const uint8_t paletteIndex = getDitheredGradientPaletteIndex(x, y, width, height, seg.speed, seg.intensity);
      seg.setPixelColorXY(x, y, seg.color_from_palette(paletteIndex, false, false, 0));
    }
  }
}

