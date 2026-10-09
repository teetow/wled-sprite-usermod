#pragma once
// The wind every moving effect shares. The host writes it once per scene as
// top-level `panel_wind`, so the airborne particles, the ground litter and
// anything else that moves with the air agree on one gust schedule and one
// air field. Until the host has sent it, each effect falls back to its own
// segment controls, so WLED's UI and presets still give a steady wind.
#include "flow_field.hpp"

struct PanelWind {
  bool valid;
  uint8_t meanTenths, gustTenths; // physical m/s x10, for the solvers
  uint8_t airMean, airGust;       // on-panel air speed, on the Wind effect's sx scale
  uint8_t activity;               // gustiness, 0..255
  uint8_t turbulence;             // eddy strength, on the Wind effect's c1 scale
  bool blowLeft;
  GustTuning tuning;
};
static PanelWind panelWind;

// What one effect sees this frame, from panelWind or its segment fallback.
struct WindInputs {
  float meanMps, gustMps; // gustMps >= meanMps
  uint8_t airMean, airGust;
  uint8_t activity, turbulence;
  bool blowLeft;
  GustTuning tuning;
};

// On-panel air speed in px/s for an sx-scale byte. sx=1 is about 3 px/s (a
// barely moving leaf), sx=128 about 31 px/s (roughly one panel width per
// second), sx=255 about 60 px/s (debris).
inline int16_t windBaseSpeed(uint8_t speed) {
  return speed == 0 ? 0 : int16_t(map(speed, 1, 255, 3, 60));
}

// Inverts Neoblaster's default sx scale (4 m/s = 70, 17 m/s = 255), for a Wind
// segment driven from WLED's UI without the host.
inline float windControlMps(uint8_t control) {
  return control < 70 ? control * (4.0f / 70) : 4.0f + (control - 70) * (13.0f / 185);
}

inline WindInputs panelWindInputs() {
  const PanelWind &wind = panelWind;
  const float mean = wind.meanTenths / 10.0f;
  return {mean, max(mean, wind.gustTenths / 10.0f), wind.airMean, max(wind.airMean, wind.airGust),
          wind.activity, wind.turbulence, wind.blowLeft, wind.tuning};
}

// A Wind segment on its own: sx is the air speed, c1 the eddies, o1 the
// direction, and the wind is steady unless a gust byte is given.
inline WindInputs windInputsFromSegment(const Segment &seg, uint8_t gust = 0, uint8_t activity = 0) {
  const uint8_t ceiling = max(seg.speed, gust);
  return {windControlMps(seg.speed), windControlMps(ceiling), seg.speed, ceiling,
          activity, seg.custom1, seg.check1, GustTuning{}};
}

// A ground Leaves segment on its own: sx and c2 are mean and gust in m/s x10,
// c1 the gustiness and o1 the direction.
inline WindInputs leafWindFromSegment(const Segment &seg, const GustTuning &tuning = DEFAULT_GUST_TUNING) {
  return {seg.speed / 10.0f, max(seg.speed, seg.custom2) / 10.0f, 0, 0,
          seg.custom1, 0, seg.check1, tuning};
}

// One air field for the whole panel, in panel pixels: a vertical slice
// (x by height). Whichever effect draws first in a frame advances it, so every
// effect in that frame reads the same air.
struct AirField {
  FlowField flow;
  uint32_t lastNow;
  bool started;
};
static AirField airField;

inline void resetPanelAir() {
  memset(&airField, 0, sizeof(airField));
}

// Kept out of line: inlined into an effect's render, this float code trips an
// internal compiler error in the ESP32 toolchain (Xtensa GCC 8.4, postreload).
__attribute__((noinline)) inline void advancePanelAir(const WindInputs &wind) {
  if (airField.started && airField.lastNow == strip.now) return;
  // Pauses and stalls never replay as a burst of steps.
  const uint32_t elapsed = airField.started ? min<uint32_t>(100, strip.now - airField.lastNow) : 0;
  airField.started = true;
  airField.lastNow = strip.now;
  FlowField &flow = airField.flow;
  flow.accumulator += elapsed / 1000.0f;
  while (flow.accumulator >= FLOW_STEP_SECONDS) {
    flow.accumulator -= FLOW_STEP_SECONDS;
    stepGroundFlow(flow, Segment::maxWidth, wind.meanMps, wind.gustMps, wind.activity, wind.blowLeft, 0,
      strip.now - uint32_t(flow.accumulator * 1000), Segment::maxHeight, windBaseSpeed(wind.airMean),
      0.35f + wind.turbulence / 128.0f, wind.tuning);
  }
}

// Panel pixel to air-grid coordinates, for flowSample.
inline float airGridX(float panelX) { return panelX / max<uint16_t>(1, Segment::maxWidth) * FLOW_WIDTH - 0.5f; }
inline float airGridY(float panelY) { return panelY / max<uint16_t>(1, Segment::maxHeight) * FLOW_HEIGHT - 0.5f; }

// The panel air at a panel pixel, in on-panel px/s: the resolved flow plus the
// gust front passing that column, as the Wind particles feel it.
inline void samplePanelAir(const WindInputs &wind, float x, float y, float &airX, float &airY) {
  const float pulse = gustFieldStrength(x, strip.now, wind.blowLeft, wind.activity, wind.tuning, wind.gustMps);
  const float kick = (windBaseSpeed(wind.airGust) - windBaseSpeed(wind.airMean)) * pulse;
  const float fx = airGridX(x), fy = airGridY(y);
  airX = flowSample(airField.flow.u, fx, fy) + (wind.blowLeft ? -kick : kick);
  airY = flowSample(airField.flow.v, fx, fy);
}
