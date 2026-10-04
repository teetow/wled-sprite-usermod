// Host harness for precipitation riding the panel air. Build and run through
// test/run-host-tests.sh.
#include "wled_stub.hpp"
#include <cassert>
#include <cstdio>
#include "../precipitation_effect.hpp"

// A 32x16 rain segment configured the way the scene configures it.
static Segment makeRain(bool snow) {
  Segment seg(32, 16);
  seg.speed = snow ? 80 : 128;
  seg.intensity = snow ? 120 : 143;
  seg.custom1 = 0;
  seg.custom2 = 128;
  seg.custom3 = 0;
  seg.check1 = snow;
  seg.colors[0] = RGBW32(200, 210, 230, 0);
  return seg;
}

// Steady wind at a given on-panel air speed, blowing right.
static WindInputs steadyWind(uint8_t air, uint8_t gust = 0, uint8_t activity = 0) {
  WindInputs wind{};
  wind.meanMps = windControlMps(air);
  wind.gustMps = windControlMps(max(air, gust));
  wind.airMean = air;
  wind.airGust = max(air, gust);
  wind.activity = activity;
  wind.turbulence = 0;
  wind.tuning = GustTuning{};
  return wind;
}

// Mean horizontal-to-vertical slant over every falling drop.
static float meanSlant(Segment &seg) {
  auto *state = reinterpret_cast<PrecipitationState *>(seg.data);
  const PrecipitationParticle *particles = getPrecipitationParticles(state);
  double sum = 0;
  unsigned count = 0;
  for (uint16_t i = 0; i < state->activeTarget; i++) {
    if (particles[i].state != 1 || particles[i].vy <= 0) continue;
    sum += double(particles[i].airVx) / 16.0 / particles[i].vy;
    count++;
  }
  return count ? float(sum / count) : 0.0f;
}

static void run(Segment &seg, const WindInputs *wind, unsigned frames) {
  currentSegment = &seg;
  for (unsigned frame = 0; frame < frames; frame++) {
    strip.now += 24;
    renderPrecipitationToSegment(seg, wind);
  }
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  Segment::maxWidth = 32;
  Segment::maxHeight = 16;

  // Without the panel wind, the fixed shear is untouched: c2=173 leans rain
  // 45/128 of its fall speed, every drop alike.
  strip.now = 1000;
  resetPanelAir();
  Segment fixed = makeRain(false);
  fixed.custom2 = 173;
  run(fixed, nullptr, 200);
  {
    auto *state = reinterpret_cast<PrecipitationState *>(fixed.data);
    const PrecipitationParticle *particles = getPrecipitationParticles(state);
    for (uint16_t i = 0; i < state->activeTarget; i++) {
      // A drop respawned after its splash has no motion until its next frame.
      if (particles[i].state != 1 || particles[i].vy == 0) continue;
      assert(particles[i].vx == int16_t(45 * max<int16_t>(8, particles[i].vy) / 128));
    }
  }
  const float fixedSlant = 45.0f / 128;

  // In steady air, the mean slant lands near the old tuned shear: 6 m/s is
  // about sx 98 on Neoblaster's scale, where the fixed shear sent c2=173.
  strip.now = 1000;
  resetPanelAir();
  const WindInputs moderate = steadyWind(98);
  Segment rain = makeRain(false);
  run(rain, &moderate, 600);
  const float airSlant = meanSlant(rain);
  printf("rain slant at 6 m/s: fixed shear %.2f, riding the air %.2f\n", fixedSlant, airSlant);
  assert(airSlant > fixedSlant * 0.6f && airSlant < fixedSlant * 1.6f);
  assert(rain.outOfBoundsWrites == 0);

  // Calm, then gusts: rain leans only while a front passes, and the lean
  // varies across the panel as the front travels.
  strip.now = 1000;
  resetPanelAir();
  const WindInputs gusty = steadyWind(0, 200, 255);
  Segment squall = makeRain(false);
  float calmest = 1e9f, wildest = 0;
  currentSegment = &squall;
  for (unsigned frame = 0; frame < 5000; frame++) {
    strip.now += 24;
    renderPrecipitationToSegment(squall, &gusty);
    if (frame > 100) {
      calmest = min(calmest, meanSlant(squall));
      // A front covers only part of the panel at once, so the steepest drop
      // shows the gust; the panel mean dilutes it.
      auto *state = reinterpret_cast<PrecipitationState *>(squall.data);
      const PrecipitationParticle *particles = getPrecipitationParticles(state);
      for (uint16_t i = 0; i < state->activeTarget; i++) {
        if (particles[i].state == 1 && particles[i].vy > 0)
          wildest = max(wildest, particles[i].airVx / 16.0f / particles[i].vy);
      }
    }
  }
  printf("gust-only rain slant: calmest panel mean %.2f, steepest drop %.2f\n", calmest, wildest);
  assert(calmest < 0.05f && wildest > 0.4f);

  // A sudden wind: rain catches up within a few frames, snow takes longer.
  auto catchUp = [](bool snow) {
    strip.now = 1000;
    resetPanelAir();
    Segment seg = makeRain(snow);
    const WindInputs calm = steadyWind(0);
    run(seg, &calm, 50);
    const WindInputs strong = steadyWind(200);
    // Hold the air at the new speed so only the drops' response is measured.
    for (float &u : airField.flow.u) u = windBaseSpeed(200);
    airField.lastNow = strip.now + 24;
    currentSegment = &seg;
    strip.now += 24;
    renderPrecipitationToSegment(seg, &strong);
    auto *state = reinterpret_cast<PrecipitationState *>(seg.data);
    const PrecipitationParticle *particles = getPrecipitationParticles(state);
    double share = 0;
    unsigned count = 0;
    for (uint16_t i = 0; i < state->activeTarget; i++) {
      if (particles[i].state != 1) continue;
      const float depth = (140 + (uint16_t(particles[i].seed) >> 1)) / 256.0f;
      share += particles[i].airVx / 16.0 / (windBaseSpeed(200) * PRECIPITATION_AIR_SHARE * depth);
      count++;
    }
    return float(share / max(1U, count));
  };
  const float rainShare = catchUp(false), snowShare = catchUp(true);
  printf("one frame after a step to strong wind: rain %.0f%%, snow %.0f%% of the way\n",
         rainShare * 100, snowShare * 100);
  assert(rainShare > 0.2f && snowShare < rainShare * 0.3f);

  // An updraft holds snow up but never lifts it, and rain ignores it.
  auto fallUnderUpdraft = [](bool snow) {
    strip.now = 1000;
    resetPanelAir();
    Segment seg = makeRain(snow);
    const WindInputs calm = steadyWind(0);
    run(seg, &calm, 50);
    for (float &v : airField.flow.v) v = -40; // upward on screen
    for (float &u : airField.flow.u) u = 0;
    currentSegment = &seg;
    float slowest = 1e9f;
    for (unsigned frame = 0; frame < 200; frame++) {
      strip.now += 24;
      airField.lastNow = strip.now; // hold the field still
      renderPrecipitationToSegment(seg, &calm);
      auto *state = reinterpret_cast<PrecipitationState *>(seg.data);
      const PrecipitationParticle *particles = getPrecipitationParticles(state);
      for (uint16_t i = 0; i < state->activeTarget; i++) {
        if (particles[i].state != 1) continue;
        assert(particles[i].vy >= 0); // never rises
        if (particles[i].vy == 0) continue; // just respawned after a splash
        const float depth = (140 + (uint16_t(particles[i].seed) >> 1)) / 256.0f;
        slowest = min(slowest, particles[i].vy / (precipitationBaseFallSpeed(seg) * depth));
      }
    }
    return slowest;
  };
  const float snowFall = fallUnderUpdraft(true), rainFall = fallUnderUpdraft(false);
  printf("slowest fall under an updraft: snow %.0f%%, rain %.0f%% of still-air speed\n",
         snowFall * 100, rainFall * 100);
  assert(snowFall < 0.5f && snowFall > PRECIPITATION_SNOW_MIN_FALL - 0.1f); // whole px/s rounding
  assert(rainFall > 0.9f);

  printf("Precipitation rides the panel air: calibrated slant, gusts, lag and lift.\n");
  return 0;
}
