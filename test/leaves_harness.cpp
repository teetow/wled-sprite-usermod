#include "wled_stub.hpp"
#include <cassert>
#include "../leaves_effect.hpp"

static void step(Segment &seg, uint32_t dt = 50) {
  strip.now += dt;
  currentSegment = &seg;
  renderLeavesToSegment(seg);
}

static LeavesState &state(Segment &seg) { return *reinterpret_cast<LeavesState *>(seg.data); }

static unsigned closeGroundPairs(Segment &seg) {
  unsigned pairs = 0;
  const auto &leaves = state(seg).particles;
  for (unsigned i = 0; i < LEAVES_CAPACITY; i++) {
    if (!leaves[i].active) continue;
    for (unsigned j = 0; j < i; j++) {
      if (!leaves[j].active) continue;
      const float rawX = fabsf(leaves[i].x - leaves[j].x);
      const float dx = min(rawX, float(seg.vWidth()) - rawX);
      const float dy = leaves[i].y - leaves[j].y;
      pairs += dx * dx + dy * dy < 2.25f;
    }
  }
  return pairs;
}

int main() {
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
  // Gust-only wind gives motion AND lulls without lifting litter off its row.
  ground.custom2 = 39;
  unsigned moving = 0, stationary = 0;
  for (unsigned i = 0; i < 1000; i++) {
    auto before = state(ground).particles[0];
    step(ground);
    auto after = state(ground).particles[0];
    if (after.y != before.y) assert(fabsf(after.x - before.x) > 4.0f);
    if (after.x == before.x) stationary++; else moving++;
  }
  assert(moving > 100 && stationary > 100);
  assert(ground.outOfBoundsWrites == 0);
  ground.speed = 76;
  ground.custom2 = 136;
  for (unsigned i = 0; i < 6000; i++) step(ground);
  assert(closeGroundPairs(ground) <= 3);
  ground.speed = ground.custom2 = 0;
  float parked[LEAVES_CAPACITY];
  for (unsigned i = 0; i < LEAVES_CAPACITY; i++) parked[i] = state(ground).particles[i].x;
  for (unsigned i = 0; i < 100; i++) step(ground);
  for (unsigned i = 0; i < LEAVES_CAPACITY; i++)
    assert(state(ground).particles[i].x == parked[i]);

  // At autumn onset there are only two ground leaves. They must not remain
  // aligned across rows, or replay the same lap with the same identity forever.
  Segment sparse(32, 6);
  sparse.intensity = 17;
  sparse.speed = sparse.custom2 = 76;
  step(sparse);
  auto &first = state(sparse).particles[0];
  auto &second = state(sparse).particles[1];
  assert(first.active && second.active);
  const float initialGap = fabsf(first.x - second.x);
  assert(min(initialGap, 32.0f - initialGap) >= 4.0f);
  first.x = second.x = 8.0f;
  first.y = 0;
  second.y = 4;
  for (unsigned frame = 0; frame < 40; frame++) step(sparse);
  float gap = fabsf(first.x - second.x);
  assert(min(gap, 32.0f - gap) >= 2.0f);
  unsigned renewals = 0;
  for (unsigned frame = 0; frame < 1000; frame++) {
    const float oldX[2] = {first.x, second.x};
    const float oldY[2] = {first.y, second.y};
    step(sparse);
    const LeafParticle *pair[2] = {&first, &second};
    for (unsigned i = 0; i < 2; i++) {
      if (pair[i]->x < oldX[i] - 4.0f) {
        renewals++;
        assert(pair[i]->y != oldY[i]);
      }
    }
  }
  assert(renewals >= 4);
  sparse.speed = sparse.custom2 = 0;
  const float parkedFirst = first.x, parkedSecond = second.x;
  for (unsigned frame = 0; frame < 100; frame++) step(sparse);
  assert(first.x == parkedFirst && second.x == parkedSecond);

  Segment sparseLeft(32, 6);
  sparseLeft.check1 = true;
  sparseLeft.intensity = 17;
  sparseLeft.speed = sparseLeft.custom2 = 76;
  step(sparseLeft);
  unsigned leftRenewals = 0;
  for (unsigned frame = 0; frame < 1000; frame++) {
    auto &leaf = state(sparseLeft).particles[0];
    const float oldX = leaf.x, oldY = leaf.y;
    step(sparseLeft);
    if (leaf.x > oldX + 4.0f) {
      leftRenewals++;
      assert(leaf.y != oldY);
    }
  }
  assert(leftRenewals >= 2);

  // Each particle occupies one pixel through the whole lifecycle, including
  // its fall. Opacity may fade, but position never splits across LEDs.
  Segment single(32, 6);
  single.intensity = 10; // less than one leaf of visual density
  single.speed = single.custom2 = 0;
  single.colors[0] = single.colors[1] = single.colors[2] = 0xcca53b;
  for (unsigned frame = 0; frame < 500; frame++) {
    step(single);
    unsigned lit = 0;
    for (unsigned y = 0; y < 6; y++) for (unsigned x = 0; x < 32; x++) lit += single.lit(x, y);
    assert(lit <= 1);
    if (frame > 20) assert(lit == 1);
  }
  assert(leafWindPixelsPerSecond(0) == 0);
  assert(leafWindPixelsPerSecond(39) > 1.3f);
  assert(leafWindPixelsPerSecond(76) > 5.2f);
  assert(leafWindPixelsPerSecond(226) > 46.0f);
  const float calmX = state(single).particles[0].x;
  for (unsigned i = 0; i < 4000; i++) step(single);
  assert(state(single).particles[0].x == calmX);
  assert(state(single).particles[0].active && state(single).particles[0].age == 15000);

  // 0/5 m/s alternates fixed and moving, whereas 5/8 moves throughout and
  // speeds up in gusts. Gale motion approaches the cross-panel wind field.
  single.custom2 = 39;
  unsigned gustMoves = 0, gustStill = 0;
  for (unsigned i = 0; i < 300; i++) {
    const float before = state(single).particles[0].x;
    step(single);
    (state(single).particles[0].x == before ? gustStill : gustMoves)++;
  }
  assert(gustMoves > 20 && gustStill > 20);
  single.speed = 39;
  single.custom2 = 76;
  float slowest = 100, fastest = 0;
  for (unsigned i = 0; i < 600; i++) {
    const float before = state(single).particles[0].x;
    step(single);
    float delta = state(single).particles[0].x - before;
    if (delta < 0) delta += 32;
    assert(delta > 0);
    slowest = min(slowest, delta);
    fastest = max(fastest, delta);
  }
  assert(fastest > slowest * 2);
  single.speed = 76;
  single.custom2 = 136;
  float briskDistance = 0;
  for (unsigned i = 0; i < 200; i++) {
    const float before = state(single).particles[0].x;
    step(single);
    float delta = state(single).particles[0].x - before;
    if (delta < 0) delta += 32;
    briskDistance += delta;
  }
  assert(briskDistance > 32 && briskDistance < 150);
  single.speed = 226;
  single.custom2 = 226;
  float galeDistance = 0;
  for (unsigned i = 0; i < 200; i++) {
    const float before = state(single).particles[0].x;
    step(single);
    float delta = state(single).particles[0].x - before;
    if (delta < 0) delta += 32;
    galeDistance += delta;
  }
  assert(galeDistance > 200 && galeDistance > briskDistance * 2);
  // Tiny geometry, clock wrap, and a stalled frame remain bounded.
  Segment tiny(1, 1);
  tiny.check2 = true;
  tiny.custom1 = 255;
  tiny.custom3 = 31;
  strip.now = UINT32_MAX - 100;
  for (unsigned i = 0; i < 1000; i++) step(tiny);
  step(tiny, 3600000);
  assert(tiny.outOfBoundsWrites == 0);
  puts("Leaves: sparse shedding, landing/fades, bare canopy, litter density, gusts, bounds and clock rollover passed.");
}
