#include "wled_stub.hpp"
#include <cassert>
#include "../leaves_effect.hpp"

// Physics checks run on the original reference tuning, so they test the
// mechanics rather than the nominal look; a separate check covers that.
static GustTuning referenceTuning() {
  GustTuning tuning;
  tuning.spacingPct = 200; tuning.durationPct = 70; tuning.attackMs = 60;
  tuning.roughnessPct = 80; tuning.responseMs = 80; tuning.settleMs = 200;
  return tuning;
}
static GustTuning stepTuning = referenceTuning();

static void step(Segment &seg, uint32_t dt = 50) {
  strip.now += dt;
  currentSegment = &seg;
  renderLeavesToSegment(seg, leafWindFromSegment(seg, stepTuning));
}

static LeavesState &state(Segment &seg) { return *reinterpret_cast<LeavesState *>(seg.data); }

// The spacing invariant as drawn: pairs of visible leaves on the same or
// touching LEDs, diagonals included. Must always be zero.
static unsigned touchingLeaves(Segment &seg) {
  unsigned pairs = 0;
  const auto &leaves = state(seg).particles;
  auto pixel = [&](const LeafParticle &leaf, int &x, int &y) {
    x = int(floorf(leaf.x + 0.5f)); y = int(floorf(leaf.y + 0.5f));
    return x >= 0 && y >= 0 && x < seg.vWidth() && y < seg.vHeight();
  };
  for (unsigned i = 0; i < LEAVES_CAPACITY; i++) {
    int ax, ay;
    if (!leaves[i].active || !pixel(leaves[i], ax, ay)) continue;
    for (unsigned j = 0; j < i; j++) {
      int bx, by;
      if (leaves[j].active && pixel(leaves[j], bx, by) && abs(ax - bx) < 2 && abs(ay - by) < 2) pairs++;
    }
  }
  return pairs;
}

static unsigned closeGroundPairs(Segment &seg) {
  unsigned pairs = 0;
  const auto &leaves = state(seg).particles;
  for (unsigned i = 0; i < LEAVES_CAPACITY; i++) {
    if (!leaves[i].active) continue;
    for (unsigned j = 0; j < i; j++) {
      if (!leaves[j].active) continue;
      const float dx = leaves[i].x - leaves[j].x; // ground litter no longer wraps
      const float dy = leaves[i].y - leaves[j].y;
      pairs += dx * dx + dy * dy < 2.25f;
    }
  }
  return pairs;
}

int main() {
  // Count births over the same ten minutes with identical seeds and canopy.
  // Seasonal taper changes frequency while each animation keeps its duration.
  auto seasonalBirths = [](uint8_t taper) {
    strip.now = 0;
    Segment segment(6, 8);
    segment.check2 = true;
    segment.intensity = segment.custom1 = 255;
    segment.custom3 = 31;
    segment.custom2 = taper;
    unsigned count = 0;
    for (unsigned frame = 0; frame < 12000; frame++) {
      step(segment);
      for (unsigned i = 0; i < state(segment).spawnerCount; i++) {
        const auto &leaf = state(segment).particles[i];
        if (leaf.active && leaf.stage == 1 && leaf.age == 0) count++;
      }
    }
    return count;
  };
  const unsigned peakBirths = seasonalBirths(0);
  const unsigned halfBirths = seasonalBirths(128);
  const unsigned lowBirths = seasonalBirths(230);
  assert(peakBirths > halfBirths && halfBirths > lowBirths);
  assert(halfBirths >= peakBirths * 0.35f && halfBirths <= peakBirths * 0.65f);
  assert(seasonalBirths(255) == 0);
  strip.now = 0;

  Segment tree(6, 8);
  tree.check2 = true;
  tree.intensity = tree.custom1 = 255;
  tree.custom3 = 20;
  tree.speed = tree.custom2 = 0;
  tree.colors[0] = tree.colors[1] = tree.colors[2] = 0xd6a82c;
  step(tree);
  assert(state(tree).spawnerCount >= 1 && state(tree).spawnerCount <= 2);
  assert(state(tree).particles[0].stage == 1);
  if (state(tree).spawnerCount == 2) {
    assert(state(tree).particles[1].wait >= 12000 && state(tree).particles[1].wait <= 22000);
  }
  uint8_t previousStage[2] = {state(tree).particles[0].stage, state(tree).particles[1].stage};
  uint32_t previousChange[2] = {strip.now, strip.now};
  float previousY[2] = {state(tree).particles[0].y, state(tree).particles[1].y};
  unsigned births = 1, landed = 0, completed = 0;
  bool sawFadeIn = false, sawFadeOut = false;
  for (unsigned frame = 0; frame < 12000; frame++) {
    step(tree);
    unsigned airborne = 0;
    unsigned litPixels = 0;
    for (unsigned y = 0; y < 8; y++) for (unsigned x = 0; x < 6; x++) litPixels += tree.lit(x, y);
    assert(litPixels <= state(tree).spawnerCount);
    for (unsigned i = 0; i < state(tree).spawnerCount; i++) {
      auto &leaf = state(tree).particles[i];
      if (leaf.stage != previousStage[i]) {
        const uint32_t duration = strip.now - previousChange[i];
        if (previousStage[i] == 1) {
          assert(leaf.stage == 2 && duration == LEAF_SPAWN_MS);
        } else if (previousStage[i] == 2) {
          assert(leaf.stage == 3 && duration == LEAF_FALL_MS);
          assert(leaf.y == 7);
          landed++;
        } else if (previousStage[i] == 3) {
          assert(leaf.stage == 0 && duration == LEAF_DESPAWN_MS);
          completed++;
          assert(leaf.wait >= 12000 && leaf.wait <= 22000);
        } else if (previousStage[i] == 0) {
          assert(leaf.stage == 1 && duration >= 12000 && duration <= 22050);
          births++;
        }
        previousStage[i] = leaf.stage;
        previousChange[i] = strip.now;
      }
      if (leaf.stage == 1) {
        assert(leaf.y == leaf.sourceY);
        assert(leafCellNoise(int(leaf.x), int(leaf.y), 1) < tree.custom1 / 255.0f);
        if (leaf.age >= 1000 && leaf.age < 2000) {
          const auto alpha = W(tree.pixel(int(leaf.x), int(leaf.y)));
          assert(alpha > 0 && alpha < 255);
          sawFadeIn = true;
        }
      }
      if (leaf.stage == 2) {
        airborne++;
        assert(leaf.y >= previousY[i] && leaf.y <= 7);
      }
      if (leaf.stage == 3) {
        assert(leaf.y == 7);
        if (leaf.age >= 8000) {
          const auto alpha = W(tree.pixel(int(leaf.x), int(leaf.y)));
          assert(alpha < 255);
          sawFadeOut = true;
        }
      }
      previousY[i] = leaf.y;
    }
    if (state(tree).spawnerCount == 2 && state(tree).particles[0].active
        && state(tree).particles[1].active)
      assert(abs(int(state(tree).particles[0].x) - int(state(tree).particles[1].x)) >= 2);
    assert(airborne <= 2);
  }
  assert(births >= 14 && landed >= 14 && completed >= 13 && sawFadeIn && sawFadeOut);
  assert(tree.outOfBoundsWrites == 0);
  // A bare tree stops producing leaves; existing cycles finish within 22 s.
  tree.custom1 = 0;
  for (unsigned i = 0; i < 500; i++) step(tree);
  for (auto &leaf : state(tree).particles) assert(!leaf.active);

  // Different activation seeds choose one or two independent spawners.
  bool sawOne = false, sawTwo = false;
  for (unsigned seed = 1; seed <= 32; seed++) {
    Segment freshTree(6, 8);
    freshTree.check2 = true;
    freshTree.intensity = freshTree.custom1 = 255;
    freshTree.custom3 = 31;
    strip.now = seed * 173;
    step(freshTree);
    sawOne |= state(freshTree).spawnerCount == 1;
    sawTwo |= state(freshTree).spawnerCount == 2;
  }
  assert(sawOne && sawTwo);

  // A leaf from the right edge must not wrap from column 5 to column 0 when
  // it lands, even with gale-force wind blowing in either direction.
  for (bool blowLeft : {false, true}) {
    Segment edge(6, 8);
    edge.check2 = true;
    edge.check1 = blowLeft;
    edge.intensity = edge.custom1 = edge.speed = edge.custom2 = 255;
    edge.custom3 = 31;
    edge.colors[0] = 0xd6a82c;
    step(edge);
    auto &runtime = state(edge);
    runtime.spawnerCount = 1;
    auto &leaf = runtime.particles[0];
    leaf = {};
    leaf.active = leaf.falling = true;
    leaf.stage = 2;
    leaf.x = 5;
    leaf.y = leaf.sourceY = 4;
    for (unsigned frame = 0; frame < (LEAF_FALL_MS + LEAF_DESPAWN_MS) / 50; frame++) {
      step(edge);
      assert(leaf.x == 5);
      assert(!edge.lit(0, 7));
    }
    assert(leaf.stage == 0);
    assert(edge.outOfBoundsWrites == 0);
  }

  // Sparse onset versus mature litter, and still air must leave positions fixed.
  Segment ground(32, 6);
  ground.intensity = 6;
  ground.speed = ground.custom2 = 0;
  step(ground);
  unsigned live = 0;
  for (auto &leaf : state(ground).particles) live += leaf.active;
  assert(live == 1);
  const float stillX = state(ground).particles[0].x;
  for (unsigned i = 0; i < 500; i++) step(ground);
  assert(state(ground).particles[0].x == stillX);
  ground.intensity = 255;
  step(ground);
  live = 0;
  for (auto &leaf : state(ground).particles) live += leaf.active;
  assert(live == 24);
  assert(closeGroundPairs(ground) == 0);
  const uint32_t groundClock = strip.now;
  for (unsigned seed = 0; seed < 20; seed++) {
    strip.now = groundClock + seed * 1777;
    Segment spread(32, 6);
    spread.intensity = 255;
    spread.speed = spread.custom2 = 0;
    step(spread);
    assert(closeGroundPairs(spread) <= 2);
  }
  strip.now = groundClock;
  // Perspective derives both screen position and speed from physical depth.
  LeavesState projected{};
  projected.width = 32; projected.height = 12; projected.groundTop = 6;
  LeafParticle far{}, near{};
  far.worldX = near.worldX = 16;
  far.depth = 0; near.depth = 1;
  projectGroundLeaf(projected, far); projectGroundLeaf(projected, near);
  assert(far.x == near.x && far.y == 6 && near.y == 11);
  far.worldX += 3; near.worldX += 3;
  projectGroundLeaf(projected, far); projectGroundLeaf(projected, near);
  assert(fabsf((near.x - 16) / (far.x - 16) - 3) < 0.001f);
  near.height = 2; projectGroundLeaf(projected, near);
  assert(near.y == 9 && near.depth == 1);

  struct Metrics { double travel = 0, lateral = 0, population = 0, busiestRow = 0; unsigned moving = 0, rest = 0, lifted = 0, above = 0, samples = 0; float height = 0; };
  auto scenario = [](uint8_t mean, uint8_t gust, uint8_t activity, bool left = false) {
    strip.now = 1000;
    Segment scene(32, 12);
    scene.custom3 = 6;
    scene.intensity = 255;
    scene.speed = mean; scene.custom2 = gust; scene.custom1 = activity;
    scene.check1 = left;
    scene.colors[0] = scene.colors[1] = scene.colors[2] = 0xcca53b;
    Metrics metrics;
    double rows[12] = {}, grounded = 0;
    for (unsigned frame = 0; frame < 2400; frame++) {
      step(scene);
      assert(touchingLeaves(scene) == 0);
      for (const auto &leaf : state(scene).particles) if (leaf.active && leaf.height == 0) {
        const int row = int(floorf(leaf.y + 0.5f));
        if (row >= 6 && row < 12) { rows[row]++; grounded++; }
      }
      for (const auto &leaf : state(scene).particles) if (leaf.active) {
        metrics.samples++;
        assert(std::isfinite(leaf.x) && std::isfinite(leaf.y));
        assert(leaf.depth >= 0 && leaf.depth <= 1);
        assert(leaf.height >= 0 && leaf.height <= 12);
        metrics.travel += fabsf(leaf.vx) * leafPerspective(leaf.depth) * 0.05;
        metrics.lateral += fabsf(leaf.vy) * 0.05;
        metrics.moving += fabsf(leaf.vx) > 0.05f;
        metrics.rest += fabsf(leaf.vx) <= 0.05f;
        metrics.lifted += leaf.height > 0.3f;
        metrics.above += leaf.y < 5.5f;
        metrics.height = max(metrics.height, leaf.height);
      }
      assert(scene.outOfBoundsWrites == 0);
    }
    // Returning to calm lets momentum and lifted leaves settle, then stops.
    scene.speed = scene.custom2 = 0;
    for (unsigned frame = 0; frame < 500; frame++) { step(scene); assert(touchingLeaves(scene) == 0); }
    for (unsigned row = 6; row < 12; row++) metrics.busiestRow = max(metrics.busiestRow, rows[row] / max(1.0, grounded));
    for (const auto &leaf : state(scene).particles) if (leaf.active) {
      assert(leaf.height == 0);
      assert(fabsf(leaf.vx) < 0.02f && fabsf(leaf.vy) < 0.02f);
    }
    // Leaves blown off the panel are gone for a while, so compare mean speed
    // per visible leaf rather than total distance across a varying population.
    metrics.population = metrics.samples / 2400.0;
    metrics.travel /= max(1U, metrics.samples) * 0.05;
    printf("ground %g/%g: speed %.2f px/s, population %.1f, resting %u, lifted %.2f%%, above horizon %.2f%%\n",
      mean / 10.0, gust / 10.0, metrics.travel, metrics.population, metrics.rest,
      100.0 * metrics.lifted / max(1U, metrics.samples), 100.0 * metrics.above / max(1U, metrics.samples));
    return metrics;
  };
  const auto calm = scenario(0, 0, 0);
  const auto gustOnly = scenario(0, 255, 255);
  assert(gustOnly.rest > gustOnly.moving && gustOnly.travel > 1);
  const auto light = scenario(10, 20, 26);
  const auto onset = scenario(30, 40, 45);
  const auto moderate = scenario(40, 70, 109);
  const auto windy = scenario(50, 80, 118);
  const auto liftOnset = scenario(80, 100, 109);
  const auto rough = scenario(80, 140, 228);
  const auto gale = scenario(150, 200, 216);
  const auto lifted = [](const Metrics &m) { return double(m.lifted) / max(1U, m.samples); };
  const auto above = [](const Metrics &m) { return double(m.above) / max(1U, m.samples); };
  assert(calm.travel == 0);
  assert(light.rest > light.moving * 20 && light.travel > 0 && light.travel < onset.travel / 10);
  assert(onset.rest > onset.moving && onset.travel > light.travel * 10);
  assert(moderate.travel > onset.travel * 2 && windy.travel > moderate.travel * 2);
  assert(rough.travel > windy.travel * 2 && gale.travel > rough.travel * 1.5);
  assert(light.lifted == 0 && moderate.lifted == 0 && windy.lifted == 0);
  assert(liftOnset.lifted > 0 && lifted(liftOnset) < lifted(rough));
  assert(lifted(rough) > 0.004 && rough.above > 20);
  assert(lifted(gale) > lifted(rough) * 3 && above(gale) > 0.01);
  // Nothing wraps: still litter keeps its density. Wind carries leaves off
  // the panel a little faster than delayed arrivals replace them, but the
  // ground never empties.
  assert(calm.population == 24 && light.population > 23);
  assert(windy.population < moderate.population && gale.population < windy.population);
  assert(gale.population > 7);
  // Moving litter spreads across depth instead of lining up on the horizon
  // or front row (even spread is 1/6 per row; edge pinning used to put
  // 25-40% on each).
  for (const auto *m : {&moderate, &windy, &rough, &gale}) assert(m->busiestRow < 0.3);
  const auto reversed = scenario(150, 200, 216, true);
  assert(reversed.travel > windy.travel && reversed.above > 0);

  // The nominal tuning (the firmware defaults) still reads as leaves in the
  // wind: litter moves at 8/14 m/s, and the spacing rule holds.
  {
    stepTuning = DEFAULT_GUST_TUNING;
    strip.now = 1000;
    Segment nominal(32, 12);
    nominal.custom3 = 6;
    nominal.intensity = 255;
    nominal.speed = 80; nominal.custom2 = 140; nominal.custom1 = 228;
    double travel = 0;
    for (unsigned frame = 0; frame < 2400; frame++) {
      step(nominal);
      assert(touchingLeaves(nominal) == 0);
      for (const auto &leaf : state(nominal).particles) if (leaf.active) travel += fabsf(leaf.vx) * 0.05;
    }
    printf("nominal tuning at 8/14: travel %.0f px\n", travel);
    assert(travel > 1000);
    stepTuning = referenceTuning();
  }

  // A leaf blown off the panel stays gone for a while instead of re-entering
  // upwind on the same frame. Its replacement may arrive anywhere.
  {
    strip.now = 1000;
    Segment gusty(32, 12);
    gusty.custom3 = 6;
    gusty.intensity = 255;
    gusty.speed = 150; gusty.custom2 = 200; gusty.custom1 = 216;
    bool wasActive[LEAVES_CAPACITY] = {};
    uint32_t goneAt[LEAVES_CAPACITY] = {};
    unsigned departures = 0, edgeArrivals = 0, placedArrivals = 0;
    for (unsigned frame = 0; frame < 4000; frame++) {
      step(gusty);
      // Ground litter never draws below half opacity, even while arriving.
      for (const auto &leaf : state(gusty).particles) if (leaf.active) {
        const int x = int(floorf(leaf.x + 0.5f)), y = int(floorf(leaf.y + 0.5f));
        if (x >= 0 && y >= 0 && x < 32 && y < 12) assert(W(gusty.pixel(x, y)) >= 127);
      }
      for (unsigned i = 0; i < LEAVES_CAPACITY; i++) {
        const auto &leaf = state(gusty).particles[i];
        if (wasActive[i] && !leaf.active) { departures++; goneAt[i] = strip.now; }
        if (!wasActive[i] && leaf.active && goneAt[i]) {
          assert(strip.now - goneAt[i] >= LEAF_RETURN_MIN_MS);
          (leaf.age >= LEAF_GROUND_FADE_MS ? edgeArrivals : placedArrivals)++;
        }
        wasActive[i] = leaf.active;
      }
    }
    printf("gale departures %u, arrivals from upwind %u, settled in place %u\n",
      departures, edgeArrivals, placedArrivals);
    assert(departures > 50 && edgeArrivals > 10 && placedArrivals > 5);
  }

  // Response and settling sliders control existing leaves, without respawns.
  strip.now = 0;
  while (gustFieldStrength(16, strip.now, false, 255) < 0.8f && strip.now < 131072) strip.now += 10;
  assert(strip.now < 131072);
  LeavesState responseState{};
  responseState.width = 32; responseState.height = 12; responseState.groundTop = 6;
  responseState.random = 0x9e3779b9U; // flutter draws from it; xorshift never leaves 0
  Segment gustTest(32, 12);
  gustTest.speed = 0; gustTest.custom2 = 255; gustTest.custom1 = 255;
  LeafParticle fast{}, slow{};
  fast.active = slow.active = true;
  fast.worldX = slow.worldX = 16;
  fast.depth = slow.depth = 0.5f;
  projectGroundLeaf(responseState, fast); projectGroundLeaf(responseState, slow);
  GustTuning quick = DEFAULT_GUST_TUNING, sluggish = DEFAULT_GUST_TUNING;
  quick.responseMs = 30; sluggish.responseMs = 1000;
  auto moveWith = [&](LeafParticle &leaf, const GustTuning &tuning) {
    responseState.wind = leafWindFromSegment(gustTest, tuning);
    moveGroundLeaf(responseState, leaf, gustTest, 0.02f);
  };
  for (unsigned frame = 0; frame < 1; frame++) {
    moveWith(fast, quick);
    moveWith(slow, sluggish);
  }
  assert(fast.vx > slow.vx * 3 && fast.vx > 10);
  gustTest.custom2 = 0;
  quick.settleMs = 50; sluggish.settleMs = 2000;
  fast.vx = slow.vx = 10;
  fast.worldX = slow.worldX = 16;
  for (unsigned frame = 0; frame < 10; frame++) {
    moveWith(fast, quick);
    moveWith(slow, sluggish);
  }
  assert(fast.vx < 0.05f && slow.vx > 5);

  // Tiny ground geometry, a moved horizon, millis wrap and a stall stay finite.
  Segment tiny(1, 1);
  tiny.intensity = tiny.speed = tiny.custom2 = tiny.custom3 = 255;
  strip.now = UINT32_MAX - 100;
  for (unsigned i = 0; i < 1000; i++) step(tiny);
  step(tiny, 3600000);
  assert(tiny.outOfBoundsWrites == 0);
  for (auto &leaf : state(tiny).particles) if (leaf.active) assert(std::isfinite(leaf.x));
  ground.custom3 = 2;
  step(ground);
  assert(state(ground).groundTop == 2);
  ground.intensity = 0; step(ground);
  for (auto &leaf : state(ground).particles) assert(!leaf.active);
  assert(sizeof(LeavesState) < 8192);
}
