#pragma once

// A downwind-moving field shared by Wind and ground Leaves. A short, nearly
// flat crest makes each gust distinct; unrelated wavelengths bend its timing
// and vary peak strength slightly without weakening it. Both effects use the
// panel clock and absolute X, so separate segments agree without shared state.
inline float gustFieldStrength(float absoluteX, uint32_t now, bool blowLeft,
                               uint8_t activity = 0) {
  const float downwindX = blowLeft ? -absoluteX : absoluteX;
  // 32-bit phase arithmetic stays precise over long uptimes and crosses the
  // millis() rollover smoothly. All three rates travel at about 10 px/s.
  const float turnScale = 1.0f / 4294967296.0f;
  const float radiansPerTurn = 6.2831853f;
  const float second = downwindX * (radiansPerTurn / 53.0f)
                     - float(uint32_t(now * 810371U)) * (radiansPerTurn * turnScale);
  const float third = downwindX * (radiansPerTurn / 31.0f)
                    - float(uint32_t(now * 1385473U)) * (radiansPerTurn * turnScale);
  float phase = float(uint32_t(now * 442780U)) * turnScale - downwindX / 97.0f
              + 0.035f * sinf(second + 1.3f) + 0.018f * sinf(third + 2.4f);
  phase -= floorf(phase);

  float pulse = 0.0f;
  if (phase >= 0.12f && phase < 0.17f) pulse = (phase - 0.12f) / 0.05f;
  else if (phase >= 0.17f && phase < 0.25f) pulse = 1.0f;
  else if (phase >= 0.25f && phase < 0.39f) pulse = (0.39f - phase) / 0.14f;

  // Mean wind opens a second, shorter front halfway through the lull. Its
  // height remains full strength; activity changes duration, not peak force.
  // Both effects receive the same activity byte from the scene controller.
  if (activity) {
    const float halfWidth = 0.16f * activity / 255.0f;
    const float start = 0.72f - halfWidth;
    const float topStart = 0.72f - 0.45f * halfWidth;
    const float topEnd = 0.72f - 0.25f * halfWidth;
    const float end = 0.72f + halfWidth;
    float secondPulse = 0.0f;
    if (phase >= start && phase < topStart)
      secondPulse = (phase - start) / (topStart - start);
    else if (phase >= topStart && phase < topEnd) secondPulse = 1.0f;
    else if (phase >= topEnd && phase < end)
      secondPulse = (end - phase) / (end - topEnd);
    pulse = max(pulse, secondPulse);
  }

  // Keep successive peaks obvious, even when their timing varies.
  const float peak = 0.95f + 0.05f * sinf(second + 0.4f);
  return pulse * peak;
}
