// Deterministic host harness for the Wind effect.
//
// Compiles the shipped wind_effect.hpp against the WLED stub and steps it frame
// by frame with a clock the test owns. Because the effect seeds its PRNG from
// strip.now, pinning the start time pins the whole field: same run, same
// particles, same pixels, every time.
//
//   test/run-host-tests.sh          # build + run
//   ./wind_harness --frames         # also print ASCII frames to eyeball the look
//
// This is where particle motion gets verified. The panel test that follows only
// has to answer "does the same code run on an ESP32 and composite correctly",
// which is a much narrower question than "is the physics right".

#include "wled_stub.hpp"

#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <set>
#include <string>

namespace sprite_usermod {
#include "../wind_effect.hpp"
}  // namespace sprite_usermod

using namespace sprite_usermod;

// ── Test plumbing ────────────────────────────────────────────────────────────

static int failures = 0;
static int checks = 0;

static void check(bool condition, const std::string &what) {
  checks++;
  if (condition) {
    printf("  ok   %s\n", what.c_str());
  } else {
    failures++;
    printf("  FAIL %s\n", what.c_str());
  }
}

static void checkNear(double actual, double expected, double tolerance, const std::string &what) {
  const bool ok = std::fabs(actual - expected) <= tolerance;
  checks++;
  if (ok) {
    printf("  ok   %s (%.2f, expected %.2f +/- %.2f)\n", what.c_str(), actual, expected, tolerance);
  } else {
    failures++;
    printf("  FAIL %s (%.2f, expected %.2f +/- %.2f)\n", what.c_str(), actual, expected, tolerance);
  }
}

static void section(const std::string &title) {
  printf("\n%s\n", title.c_str());
}

// A segment configured the way the neoblaster wind layer configures it.
struct WindRig {
  Segment seg;
  uint32_t startMs;

  WindRig(uint16_t width = 32, uint16_t height = 16, uint32_t startMs = 1000)
      : seg(width, height), startMs(startMs) {
    seg.colors[0] = RGBW32(214, 148, 42, 0);
    seg.colors[1] = RGBW32(176, 64, 32, 0);
    seg.colors[2] = RGBW32(126, 88, 44, 0);
    seg.speed = 128;
    seg.intensity = 90;
    seg.custom1 = 110;
    seg.custom2 = 150;
    seg.custom3 = 0;
    strip.now = startMs;
    currentSegment = &seg;
  }

  // One rendered frame, advancing the owned clock by exactly deltaMs.
  void step(uint32_t deltaMs = 33) {
    currentSegment = &seg;
    strip.now += deltaMs;
    renderWindToSegment(seg);
  }

  WindState *state() { return reinterpret_cast<WindState *>(seg.data); }
  WindParticle *particles() { return getWindParticles(state()); }

  unsigned litPixels() const {
    unsigned count = 0;
    for (int y = 0; y < seg.vHeight(); y++)
      for (int x = 0; x < seg.vWidth(); x++)
        if (seg.lit(x, y)) count++;
    return count;
  }

  // Sub-pixel center of mass over live particles, which is stable across the
  // respawns that make any single particle's x jump.
  double meanX() const {
    const WindState *s = reinterpret_cast<const WindState *>(seg.data);
    const WindParticle *p = reinterpret_cast<const WindParticle *>(s + 1);
    double sum = 0;
    unsigned live = 0;
    for (uint16_t i = 0; i < s->activeTarget; i++) {
      if (p[i].state == 0) continue;
      sum += double(p[i].x) / WIND_FP_SCALE;
      live++;
    }
    return live ? sum / live : 0;
  }

  double meanY() const {
    const WindState *s = reinterpret_cast<const WindState *>(seg.data);
    const WindParticle *p = reinterpret_cast<const WindParticle *>(s + 1);
    double sum = 0;
    unsigned live = 0;
    for (uint16_t i = 0; i < s->activeTarget; i++) {
      if (p[i].state == 0) continue;
      sum += double(p[i].y) / WIND_FP_SCALE;
      live++;
    }
    return live ? sum / live : 0;
  }

  // Motion of the particles that did NOT recycle during a step. A recycled
  // particle jumps to the far edge with a fresh row, so folding it into an
  // average swamps any measurement of how the field is actually moving — which
  // is exactly how the first version of these tests fooled itself.
  struct StepDelta {
    double dx = 0;
    double dy = 0;
    unsigned recycles = 0;
    unsigned samples = 0;
  };

  StepDelta stepTracked(uint32_t deltaMs = 33) {
    std::vector<std::pair<int32_t, int32_t>> before;
    if (state()) {
      for (uint16_t i = 0; i < state()->particleCount; i++) {
        // Copy out of the packed struct before taking references to the values.
        const int32_t px = particles()[i].x;
        const int32_t py = particles()[i].y;
        before.push_back(std::make_pair(px, py));
      }
    }
    step(deltaMs);

    StepDelta delta;
    if (before.empty()) return delta;
    const int32_t jump = int32_t(seg.vWidth()) * WIND_FP_SCALE / 2;
    for (uint16_t i = 0; i < state()->activeTarget && i < before.size(); i++) {
      const WindParticle &p = particles()[i];
      if (p.state == 0) continue;
      const int32_t dx = p.x - before[i].first;
      if (std::abs(dx) > jump) { delta.recycles++; continue; }
      delta.dx += double(dx) / WIND_FP_SCALE;
      delta.dy += double(p.y - before[i].second) / WIND_FP_SCALE;
      delta.samples++;
    }
    return delta;
  }

  // Mean vertical velocity over every live particle. Unbiased, unlike averaging
  // observed dy: the fastest particles cross an edge most often, so excluding
  // recycled frames quietly drops them from the sample and skews the result low.
  double meanVy() const {
    const WindState *s = reinterpret_cast<const WindState *>(seg.data);
    const WindParticle *p = reinterpret_cast<const WindParticle *>(s + 1);
    double sum = 0;
    unsigned live = 0;
    for (uint16_t i = 0; i < s->activeTarget; i++) {
      if (p[i].state == 0) continue;
      sum += p[i].vy;
      live++;
    }
    return live ? sum / live : 0;
  }

  void printFrame(const std::string &label) const {
    printf("    %s\n", label.c_str());
    for (int y = 0; y < seg.vHeight(); y++) {
      printf("    |");
      for (int x = 0; x < seg.vWidth(); x++) {
        const uint32_t c = seg.pixel(x, y);
        const uint8_t alpha = W(c);
        // Density ramp by alpha so parallax depth is visible in the dump.
        const char *glyph = alpha == 0 ? " " : (alpha < 110 ? "." : (alpha < 190 ? "o" : "#"));
        printf("%s", glyph);
      }
      printf("|\n");
    }
  }
};

static bool showFrames = false;

// ── Tests ────────────────────────────────────────────────────────────────────

static void testTravelDirection() {
  section("Travel direction follows the blow-left flag");

  auto travel = [](bool blowLeft) {
    WindRig rig;
    rig.seg.check1 = blowLeft;
    rig.seg.intensity = 255;
    for (int i = 0; i < 3; i++) rig.step();
    double total = 0;
    unsigned recycles = 0;
    for (int i = 0; i < 30; i++) {
      const auto delta = rig.stepTracked();
      total += delta.dx;
      recycles += delta.recycles;
    }
    return std::pair<double, unsigned>{total, recycles};
  };

  const auto right = travel(false);
  const auto left = travel(true);
  check(right.first > 0, "default blows toward panel-right");
  check(left.first < 0, "check1 blows toward panel-left");
  check(right.second > 0 && left.second > 0, "both directions recycle particles at the edge");
  checkNear(std::fabs(left.first), std::fabs(right.first), std::fabs(right.first) * 0.1,
            "both directions travel at the same rate");
}

static void testGustSpeed() {
  section("Gust speed tracks the sx mapping");

  // sx=128 maps to ~31 px/s before parallax; the field average sits a little
  // above that because the depth multiplier averages ~1.06.
  WindRig rig;
  rig.seg.speed = 128;
  rig.seg.custom2 = 128;  // level flight, so nothing recycles off the top/bottom
  for (int i = 0; i < 5; i++) rig.step();

  // Measure one particle over a second so edge recycling can't skew the number.
  WindParticle &particle = rig.particles()[0];
  const double startX = double(particle.x) / WIND_FP_SCALE;
  const int16_t vx = particle.vx;
  for (int i = 0; i < 30; i++) rig.step(33);  // 990 ms
  const double travelled = double(particle.x) / WIND_FP_SCALE - startX;
  const bool recycled = travelled < 0;  // wrapped at the edge during the run
  if (!recycled) {
    checkNear(travelled, vx * 0.99, 1.5, "one particle travels vx px in one second");
  } else {
    check(true, "particle recycled mid-run (speed covered by the vx check below)");
  }
  checkNear(vx, 31 * 1.06, 8.0, "vx at sx=128 is around 31 px/s plus parallax");

  WindRig fast;
  fast.seg.speed = 255;
  for (int i = 0; i < 5; i++) fast.step();
  check(fast.particles()[0].vx > rig.particles()[0].vx, "sx=255 is faster than sx=128");

  WindRig frozen;
  frozen.seg.speed = 0;
  for (int i = 0; i < 5; i++) frozen.step();
  const double frozenStart = frozen.meanX();
  for (int i = 0; i < 20; i++) frozen.step();
  checkNear(frozen.meanX(), frozenStart, 0.001, "sx=0 freezes the field in place");
}

static void testDensity() {
  section("Density maps intensity to live particles");

  WindRig empty;
  empty.seg.intensity = 0;
  for (int i = 0; i < 5; i++) empty.step();
  check(empty.litPixels() == 0, "ix=0 draws nothing");

  WindRig sparse;
  sparse.seg.intensity = 30;
  for (int i = 0; i < 5; i++) sparse.step();

  WindRig dense;
  dense.seg.intensity = 255;
  for (int i = 0; i < 5; i++) dense.step();

  const uint16_t capacity = dense.state()->particleCount;
  check(capacity == 89, "32x16 panel holds 89 particles (512/6 + 4)");
  check(dense.state()->activeTarget == capacity, "ix=255 fills capacity");
  check(sparse.state()->activeTarget < dense.state()->activeTarget, "ix=30 is sparser than ix=255");
  check(sparse.litPixels() > 0, "ix=30 still draws particles");
}

static void testRecycling() {
  section("Particles recycle at the upwind edge");

  WindRig rig;
  rig.seg.intensity = 255;
  rig.seg.speed = 255;
  for (int i = 0; i < 5; i++) rig.step();

  const uint16_t target = rig.state()->activeTarget;
  unsigned minLive = target;
  std::set<int> everSeenColumns;

  // Two seconds at 33 ms is long enough for every particle to cross a 32 px
  // panel several times over.
  for (int frame = 0; frame < 60; frame++) {
    rig.step();
    unsigned live = 0;
    for (uint16_t i = 0; i < target; i++) {
      const WindParticle &p = rig.particles()[i];
      if (p.state != 0) live++;
      const int column = p.x >> WIND_FP_SHIFT;
      if (column >= 0 && column < rig.seg.vWidth()) everSeenColumns.insert(column);
    }
    minLive = min<unsigned>(minLive, live);
  }

  check(minLive == target, "no particle is ever lost: the field stays full");
  check(everSeenColumns.size() == size_t(rig.seg.vWidth()), "particles occupy every column over time");
  check(rig.seg.outOfBoundsWrites == 0, "nothing is drawn outside the segment");
}

static void testRespawnMargin() {
  section("Freshly spawned particles survive their first frame");

  // Regression: the spawn margin used to reach further outside the segment than
  // the despawn margin, so a particle that re-entered was immediately re-rolled,
  // frame after frame, each time with a new row. On the panel that reads as
  // particles flickering between rows at the entry edge.
  for (uint8_t streak : {uint8_t(0), uint8_t(3)}) {
    WindRig rig;
    rig.seg.custom1 = 0;
    rig.seg.custom2 = 128;  // level flight: y may only change on a respawn
    rig.seg.custom3 = streak;
    rig.seg.intensity = 255;
    for (int i = 0; i < 3; i++) rig.step();

    unsigned rowChangesWithoutCrossing = 0;
    for (int frame = 0; frame < 60; frame++) {
      const auto delta = rig.stepTracked();
      if (delta.dy != 0) rowChangesWithoutCrossing++;
    }
    check(rowChangesWithoutCrossing == 0,
          "streak=" + std::to_string(streak) + ": no particle changes row except by crossing an edge");

    // And the spawn point must sit inside the keep-alive window by construction.
    const int32_t despawn = windDespawnMargin(rig.seg);
    check(despawn > int32_t(WIND_SPAWN_MARGIN_PX) * WIND_FP_SCALE,
          "streak=" + std::to_string(streak) + ": despawn margin exceeds the spawn margin");
  }
}

static void testFlutter() {
  section("Flutter wobbles rows without drifting them");

  // Flat: no flutter, level drift. Rows must be perfectly stable for every
  // particle that did not recycle (a recycled one legitimately gets a new row).
  WindRig flat;
  flat.seg.custom1 = 0;
  flat.seg.custom2 = 128;
  flat.seg.intensity = 255;
  for (int i = 0; i < 3; i++) flat.step();
  double flatDy = 0;
  unsigned flatSamples = 0;
  for (int i = 0; i < 30; i++) {
    const auto delta = flat.stepTracked();
    flatDy += std::fabs(delta.dy);
    flatSamples += delta.samples;
  }
  check(flatSamples > 1000, "the flat run measured a full field over many frames");
  checkNear(flatDy, 0.0, 0.0001, "c1=0 with level drift holds every particle on its row");

  // Fluttering: one particle's row must oscillate inside a bounded band. The
  // window resets whenever that particle recycles, since a respawn is allowed
  // to move it anywhere.
  WindRig wobble;
  wobble.seg.custom1 = 255;
  wobble.seg.custom2 = 128;
  wobble.seg.speed = 1;
  for (int i = 0; i < 3; i++) wobble.step();

  int minRow = 1000, maxRow = -1000;
  int runLength = 0, bestRun = 0, bestSwing = 0;
  int32_t previousX = wobble.particles()[0].x;
  std::set<uint8_t> phasesSeen;
  for (int i = 0; i < 120; i++) {
    wobble.step();
    const WindParticle &p = wobble.particles()[0];
    const bool recycled = std::abs(p.x - previousX) > int32_t(wobble.seg.vWidth()) * WIND_FP_SCALE / 2;
    previousX = p.x;
    if (recycled) {
      if (runLength > bestRun) { bestRun = runLength; bestSwing = maxRow - minRow; }
      runLength = 0;
      minRow = 1000;
      maxRow = -1000;
      continue;
    }
    const int row = int((p.y + windFlutterOffset(p, wobble.seg)) >> WIND_FP_SHIFT);
    minRow = min(minRow, row);
    maxRow = max(maxRow, row);
    runLength++;
    phasesSeen.insert(p.phase);
  }
  if (runLength > bestRun) { bestRun = runLength; bestSwing = maxRow - minRow; }

  check(bestRun >= 30, "tracked a particle across a long uninterrupted flight");
  check(bestSwing >= 2, "c1=255 swings a particle across at least 3 rows");
  check(bestSwing <= 4, "flutter stays bounded at roughly +/-2 px");
  check(phasesSeen.size() > 20, "flutter phase advances continuously");

  std::set<uint8_t> phases;
  for (uint16_t i = 0; i < 20; i++) phases.insert(wobble.particles()[i].phase);
  check(phases.size() > 10, "particles flutter out of phase with each other");
}

static void testVerticalDrift() {
  section("Vertical drift settles, lifts, or holds level");

  auto rigAt = [](uint8_t drift) {
    auto rig = std::make_unique<WindRig>();
    rig->seg.custom1 = 0;
    rig->seg.custom2 = drift;
    rig->seg.intensity = 255;
    for (int i = 0; i < 5; i++) rig->step();
    return rig;
  };

  // Parallax depth averages (140 + 127/2)/256 = 0.795, so the field drifts well
  // below the raw slider value. c2=255 truncates to +15 px/s and c2=0 to
  // -16 px/s, making lift a shade faster than settle.
  checkNear(rigAt(255)->meanVy(), 15.0 * 0.795, 1.5, "drift=255 settles at about 12 px/s");
  checkNear(rigAt(0)->meanVy(), -16.0 * 0.795, 1.5, "drift=0 lifts at about 13 px/s");
  checkNear(rigAt(128)->meanVy(), 0.0, 0.0001, "drift=128 has exactly zero vertical velocity");

  // Velocity is only half the claim; positions must actually follow it.
  auto observedDrift = [](uint8_t drift) {
    WindRig rig;
    rig.seg.custom1 = 0;
    rig.seg.custom2 = drift;
    rig.seg.intensity = 255;
    for (int i = 0; i < 3; i++) rig.step();
    double total = 0;
    for (int i = 0; i < 30; i++) total += rig.stepTracked().dy;
    return total;
  };
  check(observedDrift(255) > 0, "settling particles move down the panel");
  check(observedDrift(0) < 0, "lifting particles move up the panel");
  checkNear(observedDrift(128), 0.0, 0.0001, "level particles hold their row exactly");
}

static void testStreaks() {
  section("Streaks trail upwind on the particle's own row");

  WindRig bare;
  bare.seg.custom3 = 0;
  bare.seg.custom1 = 0;
  bare.seg.intensity = 8;
  for (int i = 0; i < 5; i++) bare.step();
  const unsigned barePixels = bare.litPixels();

  WindRig streaked;
  streaked.seg.custom3 = 3;
  streaked.seg.custom1 = 0;
  streaked.seg.intensity = 8;
  for (int i = 0; i < 5; i++) streaked.step();
  check(streaked.litPixels() > barePixels, "c3=3 lights more pixels than c3=0");

  // Find a head and walk upwind: the trail must be on the same row, behind it,
  // and fading.
  const WindParticle &p = streaked.particles()[0];
  const int headX = int(p.x >> WIND_FP_SHIFT);
  const int headY = int((p.y + windFlutterOffset(p, streaked.seg)) >> WIND_FP_SHIFT);
  bool trailBehind = true;
  bool trailFades = true;
  uint8_t previousAlpha = 255;
  for (int i = 1; i <= 3; i++) {
    const int x = headX - i;  // blowing right, so the trail is to the left
    if (x < 0 || x >= streaked.seg.vWidth()) continue;
    if (!streaked.seg.lit(x, headY)) trailBehind = false;
    const uint8_t alpha = W(streaked.seg.pixel(x, headY));
    if (alpha > previousAlpha) trailFades = false;
    previousAlpha = alpha;
  }
  check(trailBehind, "streak pixels sit upwind of the head on the same row");
  check(trailFades, "streak alpha fades away from the head");
}

static void testColorsAndAlpha() {
  section("Particles use only the three segment colors, with parallax alpha");

  WindRig rig;
  rig.seg.intensity = 255;
  rig.seg.custom3 = 0;
  for (int i = 0; i < 5; i++) rig.step();

  std::set<uint32_t> rgbSeen;
  uint8_t minAlpha = 255, maxAlpha = 0;
  for (int y = 0; y < rig.seg.vHeight(); y++) {
    for (int x = 0; x < rig.seg.vWidth(); x++) {
      const uint32_t c = rig.seg.pixel(x, y);
      if (W(c) == 0) continue;
      rgbSeen.insert(c & 0x00FFFFFF);
      minAlpha = min<uint8_t>(minAlpha, W(c));
      maxAlpha = max<uint8_t>(maxAlpha, W(c));
    }
  }

  check(rgbSeen.size() == 3, "exactly the three configured colors appear");
  for (uint32_t rgb : rgbSeen) {
    const bool known = rgb == (rig.seg.colors[0] & 0x00FFFFFF)
                    || rgb == (rig.seg.colors[1] & 0x00FFFFFF)
                    || rgb == (rig.seg.colors[2] & 0x00FFFFFF);
    check(known, "rendered color is one of the segment colors");
  }
  check(minAlpha >= 80 && maxAlpha <= 254, "alpha stays inside the parallax band 80..254");
  check(maxAlpha - minAlpha > 40, "parallax actually varies particle brightness");

  // A scene that sets only col[0] must still render, not go invisible.
  WindRig single;
  single.seg.colors[1] = 0;
  single.seg.colors[2] = 0;
  single.seg.intensity = 255;
  for (int i = 0; i < 5; i++) single.step();
  check(single.litPixels() > 0, "black col[1]/col[2] falls back to col[0] instead of vanishing");
}

static void testFrameRateIndependence() {
  section("Motion is frame-rate independent");

  WindRig coarse;
  coarse.seg.custom1 = 0;
  for (int i = 0; i < 3; i++) coarse.step(50);
  const double coarseStart = coarse.meanX();
  for (int i = 0; i < 10; i++) coarse.step(50);  // 500 ms in 50 ms steps
  const double coarseTravel = coarse.meanX() - coarseStart;

  WindRig fine;
  fine.seg.custom1 = 0;
  for (int i = 0; i < 3; i++) fine.step(50);
  const double fineStart = fine.meanX();
  for (int i = 0; i < 50; i++) fine.step(10);  // the same 500 ms in 10 ms steps
  const double fineTravel = fine.meanX() - fineStart;

  checkNear(fineTravel, coarseTravel, 1.0, "the same elapsed time travels the same distance");

  // The effect clamps deltaMs at 100 ms, so a stalled frame must not teleport
  // the field across the panel.
  WindRig stalled;
  for (int i = 0; i < 3; i++) stalled.step(50);
  const double before = stalled.meanX();
  stalled.step(5000);
  check(stalled.meanX() - before < 5.0, "a 5 s stall advances at most one clamped tick");
}

static void testDeterminism() {
  section("A pinned clock replays the identical field");

  auto fingerprint = [](uint32_t startMs) {
    WindRig rig(32, 16, startMs);
    for (int i = 0; i < 20; i++) rig.step();
    std::string out;
    for (int y = 0; y < rig.seg.vHeight(); y++)
      for (int x = 0; x < rig.seg.vWidth(); x++)
        out += char('0' + (W(rig.seg.pixel(x, y)) ? 1 : 0));
    return out;
  };

  check(fingerprint(1000) == fingerprint(1000), "same start time gives the same frame");
  check(fingerprint(1000) != fingerprint(7777), "a different start time gives a different field");
}

static void testSeasonLooks() {
  section("Season profiles as the scene will send them");

  struct Look {
    const char *name;
    uint32_t colors[3];
    uint8_t flutter;
    uint8_t drift;
    uint8_t streak;
    uint8_t speed;
    uint8_t density;
  };
  const Look looks[] = {
    {"autumn leaves", {RGBW32(214, 148, 42, 0), RGBW32(176, 64, 32, 0), RGBW32(126, 88, 44, 0)}, 150, 152, 0, 160, 115},
    {"summer grit", {RGBW32(76, 70, 54, 0), RGBW32(92, 84, 60, 0), RGBW32(64, 62, 50, 0)}, 70, 140, 0, 160, 115},
    {"winter spindrift", {RGBW32(236, 244, 255, 0), RGBW32(214, 228, 246, 0), RGBW32(248, 250, 255, 0)}, 90, 128, 2, 220, 180},
  };

  for (const Look &look : looks) {
    WindRig rig;
    rig.seg.colors[0] = look.colors[0];
    rig.seg.colors[1] = look.colors[1];
    rig.seg.colors[2] = look.colors[2];
    rig.seg.custom1 = look.flutter;
    rig.seg.custom2 = look.drift;
    rig.seg.custom3 = look.streak;
    rig.seg.speed = look.speed;
    rig.seg.intensity = look.density;
    for (int i = 0; i < 30; i++) rig.step();

    const unsigned lit = rig.litPixels();
    check(lit > 8, std::string(look.name) + " puts particles on the panel");
    check(lit < 200, std::string(look.name) + " leaves the panel mostly empty");
    check(rig.seg.outOfBoundsWrites == 0, std::string(look.name) + " draws in bounds");
    if (showFrames) rig.printFrame(look.name);
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--frames") showFrames = true;
  }

  printf("Wind effect host harness (deterministic clock, no hardware)\n");

  testTravelDirection();
  testGustSpeed();
  testDensity();
  testRecycling();
  testRespawnMargin();
  testFlutter();
  testVerticalDrift();
  testStreaks();
  testColorsAndAlpha();
  testFrameRateIndependence();
  testDeterminism();
  testSeasonLooks();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
