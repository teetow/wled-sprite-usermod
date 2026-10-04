#include "wled_stub.hpp"
#include <cassert>
#include "../flow_field.hpp"
#include "../wind_effect.hpp"

static double energy(const FlowField &field) {
  double result = 0;
  for (int i = 0; i < FLOW_CELLS; i++) result += field.u[i] * field.u[i] + field.v[i] * field.v[i];
  return result;
}

static double divergenceNorm(const FlowField &field, float dx, float dy) {
  double result = 0;
  for (int y = 1; y < FLOW_HEIGHT - 1; y++) for (int x = 0; x < FLOW_WIDTH; x++) {
    const float div = (field.u[flowIndex(x + 1, y)] - field.u[flowIndex(x - 1, y)]) / (2 * dx)
      + (field.v[flowIndex(x, y + 1)] - field.v[flowIndex(x, y - 1)]) / (2 * dy);
    result += div * div;
  }
  return result;
}

int main() {
  // A converging velocity field loses divergence through pressure projection.
  FlowField projected{};
  for (int y = 0; y < FLOW_HEIGHT; y++) for (int x = 0; x < FLOW_WIDTH; x++) {
    projected.u[flowIndex(x, y)] = 10 * std::sin(x * 6.2831853f / FLOW_WIDTH);
  }
  const double before = divergenceNorm(projected, 6, 3);
  projectFlow(projected, 6, 3);
  assert(divergenceNorm(projected, 6, 3) < before * 0.4);

  // Sustained wind alone develops rotational flow; energy persists after
  // forcing stops, then decays. This rules out a stateless path/noise effect.
  FlowField field{};
  for (uint32_t now = 0; now < 5000; now += 33) stepGroundFlow(field, 32, 10, 10, 0, false, 0, now);
  const double activeEnergy = energy(field);
  float maxCrossFlow = 0;
  for (int i = 0; i < FLOW_CELLS; i++) maxCrossFlow = max(maxCrossFlow, fabsf(field.v[i]));
  assert(activeEnergy > 1000 && maxCrossFlow > 0.5f);
  stepGroundFlow(field, 32, 0, 0, 0, false, 0, 5033);
  assert(energy(field) > activeEnergy * 0.5);
  for (uint32_t now = 5066; now < 20000; now += 33) stepGroundFlow(field, 32, 0, 0, 0, false, 0, now);
  assert(energy(field) < activeEnergy * 0.01);

  // Cadence checks use the original reference tuning, so they test how the
  // schedule scales rather than whatever the nominal defaults are.
  GustTuning reference;
  reference.spacingPct = 200; reference.durationPct = 70; reference.attackMs = 60;
  reference.roughnessPct = 80; reference.responseMs = 80; reference.settleMs = 200;
  struct Cadence { unsigned events = 0, active = 0; float roughness = 0; };
  auto cadence = [&](uint8_t activity, const GustTuning &tuning) {
    Cadence result;
    float previous = 0;
    for (uint32_t now = 0; now < 300000; now += 20) {
      const float pulse = gustFieldStrength(16, now, false, activity, tuning);
      assert(pulse >= 0 && pulse <= 1);
      result.events += pulse > 0 && previous == 0;
      result.active += pulse > 0;
      result.roughness += fabsf(pulse - previous);
      previous = pulse;
    }
    return result;
  };
  const auto weak = cadence(26, reference), moderate = cadence(109, reference), rough = cadence(228, reference);
  printf("cadence weak %u/%u, moderate %u/%u, rough %u/%u\n", weak.events, weak.active, moderate.events, moderate.active, rough.events, rough.active);
  assert(weak.events >= 4 && weak.events <= 8);
  assert(weak.active < 400);
  assert(moderate.events > weak.events * 2 && rough.events > moderate.events * 1.5);
  assert(rough.active > moderate.active && rough.roughness > moderate.roughness * 1.5);
  GustTuning spaced = reference;
  spaced.spacingPct = 400;
  assert(cadence(228, spaced).events < rough.events * 0.6);
  GustTuning shortBurst = reference;
  shortBurst.durationPct = 25;
  assert(cadence(228, shortBurst).active < rough.active * 0.5);
  GustTuning smooth = reference, jagged = reference;
  smooth.roughnessPct = 0; jagged.roughnessPct = 100;
  assert(cadence(228, jagged).roughness > cadence(228, smooth).roughness);
  GustTuning slowAttack = reference, fastAttack = reference;
  slowAttack.attackMs = 500; fastAttack.attackMs = 20;
  float sharpestFast = 0, sharpestSlow = 0;
  for (uint32_t t = 0; t < 100000; t += 10) {
    sharpestFast = max(sharpestFast, gustFieldStrength(16, t + 10, false, 228, fastAttack)
      - gustFieldStrength(16, t, false, 228, fastAttack));
    sharpestSlow = max(sharpestSlow, gustFieldStrength(16, t + 10, false, 228, slowAttack)
      - gustFieldStrength(16, t, false, 228, slowAttack));
  }
  assert(sharpestFast > sharpestSlow * 1.5f);
  for (int x = 0; x < 32; x++) {
    assert(fabsf(gustFieldStrength(x, UINT32_MAX, false, 228) - gustFieldStrength(x, 0, false, 228)) < 0.02f);
  }

  // Runtime airborne mode samples evolving flow, without applying per-particle sine flutter.
  strip.now = 1000;
  Segment air(32, 16);
  air.check2 = true;
  air.speed = 127; air.intensity = 255; air.custom1 = 128; air.custom2 = 128;
  currentSegment = &air;
  unsigned crossFlowFrames = 0;
  for (unsigned frame = 0; frame < 900; frame++) {
    strip.now += 33;
    renderWindToSegment(air, 212, -1, 228);
    auto &runtime = *reinterpret_cast<WindState *>(air.data);
    for (unsigned i = 0; i < runtime.particleCount; i++) {
      const auto &leaf = getWindParticles(&runtime)[i];
      crossFlowFrames += fabsf(leaf.motionY) > 0.1f;
      assert(std::isfinite(leaf.motionX) && std::isfinite(leaf.motionY));
    }
  }
  assert(crossFlowFrames > 100 && air.outOfBoundsWrites == 0);
  printf("Fluid projection, decaying wakes, gust cadence and airborne transport passed.\n");
}
