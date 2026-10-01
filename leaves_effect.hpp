// Persistent autumn litter and occasional canopy shedding. The host owns the
// calendar, snow coverage and tree palette; this effect owns only motion/time.
// Two instances share this effect: a ground band and a narrow tree-foot strip.
#include "gust_field.hpp"

constexpr uint32_t LEAVES_MAGIC = 0x4c454148UL;
constexpr unsigned LEAVES_CAPACITY = 32;
constexpr unsigned LEAVES_SHED_CAPACITY = 2;
constexpr uint32_t LEAF_SPAWN_MS = 10000;
constexpr uint32_t LEAF_FALL_MS = 2000;
constexpr uint32_t LEAF_DESPAWN_MS = 10000;

struct LeafParticle {
  float x, y, sourceY;
  uint32_t age, wait;
  uint8_t color, stage;
  bool active, falling;
};

struct LeavesState {
  uint32_t magic, lastUpdate, random;
  uint16_t width, height;
  bool shedding;
  uint8_t spawnerCount;
  LeafParticle particles[LEAVES_CAPACITY];
};

uint32_t leafRandom(LeavesState &state, uint32_t limit) {
  state.random ^= state.random << 13;
  state.random ^= state.random >> 17;
  state.random ^= state.random << 5;
  return limit ? state.random % limit : 0;
}

// Same coordinate ranks as Neoblaster's tree.ts: emit only from a leaf still
// present in the generated 6x7 canopy, and only once that leaf has turned.
float leafCellNoise(int x, int y, int salt) {
  uint32_t h = uint32_t(x + 1) * 0x27d4eb2dU ^ uint32_t(y + 1) * 0x165667b1U ^ uint32_t(salt + 1) * 0x9e3779b9U;
  h ^= h >> 15; h *= 0x85ebca6bU;
  h ^= h >> 13; h *= 0xc2b2ae35U;
  h ^= h >> 16;
  return double(h) / 4294967296.0;
}

bool spawnShedLeaf(LeavesState &state, const Segment &seg, LeafParticle &leaf) {
  static const char *canopy[] = {"..LL..", ".LLLL.", "LLLLLL", "LLLLLL", ".L.LL."};
  uint8_t candidates[30];
  unsigned count = 0;
  for (int y = 0; y < 5 && y < state.height - 1; y++) {
    for (int x = 0; x < 6 && x < state.width; x++) {
      const float turn = max(0.0f, min(0.9f, leafCellNoise(x, y, 2) * 0.75f - (1.0f - y / 7.0f) * 0.1f));
      if (canopy[y][x] == 'L' && leafCellNoise(x, y, 1) < seg.custom1 / 255.0f
          && seg.custom3 / 31.0f > turn + 0.125f) candidates[count++] = y * 6 + x;
    }
  }
  if (!count) return false;
  uint8_t spaced[30];
  unsigned spacedCount = 0;
  for (unsigned i = 0; i < count; i++) {
    const int column = candidates[i] % 6;
    bool separated = true;
    for (unsigned j = 0; j < state.spawnerCount; j++) {
      const LeafParticle &other = state.particles[j];
      if (&other != &leaf && other.active && abs(column - int(other.x)) < 2) {
        separated = false;
        break;
      }
    }
    if (separated) spaced[spacedCount++] = candidates[i];
  }
  const unsigned cell = spacedCount ? spaced[leafRandom(state, spacedCount)]
                                    : candidates[leafRandom(state, count)];
  leaf = {};
  leaf.x = cell % 6;
  leaf.y = cell / 6;
  leaf.sourceY = leaf.y;
  leaf.color = unsigned(leafCellNoise(cell % 6, cell / 6, 3) * 3) % 3;
  leaf.stage = 1;
  leaf.active = true;
  return true;
}

float groundCandidateDistance(const LeavesState &state, const LeafParticle &leaf,
                              float target, float x, float y) {
  float nearest = 1.0e9f;
  for (unsigned i = 0; i < LEAVES_CAPACITY && i < target; i++) {
    const LeafParticle &other = state.particles[i];
    if (!other.active || &other == &leaf) continue;
    const float rawX = fabsf(x - other.x);
    const float dx = min(rawX, float(state.width) - rawX);
    const float dy = y - other.y;
    // With only a few leaves, different rows must not excuse matching columns.
    nearest = min(nearest, dx * dx + (target <= 8.0f ? 0.0f : dy * dy));
  }
  return nearest;
}

void spawnGroundLeaf(LeavesState &state, LeafParticle &leaf, float target) {
  // Best-candidate blue noise: place each new leaf as far as possible from
  // visible litter, including across the horizontal wrap seam. A fixed set of
  // random candidates keeps this cheap and still fills tiny/full segments.
  float bestDistance = -1.0f;
  uint16_t bestX = 0, bestY = 0;
  for (unsigned trial = 0; trial < 24; trial++) {
    const uint16_t x = leafRandom(state, state.width);
    const uint16_t y = leafRandom(state, state.height);
    const float nearest = groundCandidateDistance(state, leaf, target, x, y);
    if (nearest > bestDistance) {
      bestDistance = nearest;
      bestX = x;
      bestY = y;
    }
    if (nearest == 1.0e9f) break; // the first leaf remains uniformly random
  }
  leaf = {};
  leaf.x = bestX + leafRandom(state, 64) / 256.0f;
  leaf.y = bestY;
  leaf.color = leafRandom(state, 3);
  leaf.active = true;
}

void renewGroundLeaf(LeavesState &state, LeafParticle &leaf, float target, bool blowLeft) {
  // An outgoing leaf is replaced by a different one entering from upwind.
  // Row, color and a few pixels of entry jitter break the endless wrap loop.
  const uint16_t oldRow = uint16_t(leaf.y);
  float bestDistance = -1.0f;
  float bestX = 0.0f;
  uint16_t bestY = 0;
  const float entrySpan = min(4.0f, float(state.width));
  for (unsigned trial = 0; trial < 12; trial++) {
    const float inset = entrySpan * leafRandom(state, 1024) / 1024.0f;
    float x = blowLeft ? state.width - 1.0f - inset : inset;
    if (x < 0) x += state.width;
    const uint16_t y = state.height > 1
      ? (oldRow + 1 + leafRandom(state, state.height - 1)) % state.height : 0;
    const float nearest = groundCandidateDistance(state, leaf, target, x, y);
    if (nearest > bestDistance) {
      bestDistance = nearest;
      bestX = x;
      bestY = y;
    }
    if (nearest == 1.0e9f) break;
  }
  leaf.x = bestX;
  leaf.y = bestY;
  leaf.color = leafRandom(state, 3);
  leaf.age = 15000; // The new leaf is already visible as it enters the panel.
}

// Keep each leaf on exactly one LED. Opacity changes only for its lifecycle;
// subpixel position is retained internally so light wind advances a pixel
// occasionally, without lighting two neighboring LEDs during the transition.
void drawLeaf(Segment &seg, float x, float y, uint32_t color, float opacity, bool wrapX = false) {
  int px = int(floorf(x + 0.5f));
  const int py = int(floorf(y + 0.5f));
  if (wrapX && px >= seg.vWidth()) px = 0;
  if (px < 0 || py < 0 || px >= seg.vWidth() || py >= seg.vHeight()) return;
  const uint8_t alpha = uint8_t(255 * max(0.0f, min(1.0f, opacity)));
  if (!alpha) return;
  // Combine overlapping particles instead of letting a faint one erase one.
  const uint32_t old = seg.getPixelColorXY(px, py);
  const unsigned combined = alpha + unsigned(W(old)) * (255 - alpha) / 255;
  const uint32_t rgb = color_blend(old, color, uint8_t(unsigned(alpha) * 255 / combined));
  seg.setPixelColorXY(px, py, RGBW32(R(rgb), G(rgb), B(rgb), combined));
}

// Neoblaster applies ground friction against the Wind effect's airborne speed.
// A quadratic control keeps low-speed motion precise within one byte.
float leafWindPixelsPerSecond(uint8_t speed) {
  const float fraction = speed / 255.0f;
  return 60.0f * fraction * fraction;
}

void spaceMovingGroundLeaves(LeavesState &state, float target, const float *motion, float seconds) {
  // The moving gust field can gather leaves into a traveling pack. Keep a
  // small blue-noise exclusion around each visible leaf, moving only leaves
  // that wind is already moving. Sparse litter also avoids aligned columns
  // across different rows, since those leaves would sample identical gusts.
  for (unsigned pass = 0; pass < 8; pass++) {
    bool adjusted = false;
    for (unsigned i = 0; i < LEAVES_CAPACITY && i < target; i++) {
      LeafParticle &a = state.particles[i];
      if (!a.active) continue;
      for (unsigned j = 0; j < i; j++) {
        LeafParticle &b = state.particles[j];
        if (!b.active || (!motion[i] && !motion[j])) continue;
        const float rowGap = fabsf(a.y - b.y);
        const float sparseGap = target <= 8.0f
          ? min(4.0f, float(state.width) / max(1.0f, target) * 0.5f) : 0.0f;
        if (rowGap >= 2.0f && sparseGap == 0) continue;
        const float neededX = max(rowGap < 0.5f ? 1.6f : rowGap < 2.0f ? 1.25f : 0.0f,
                                  sparseGap);
        float dx = a.x - b.x;
        if (dx > state.width / 2.0f) dx -= state.width;
        if (dx < -state.width / 2.0f) dx += state.width;
        const float overlap = neededX - fabsf(dx);
        if (overlap <= 0) continue;
        adjusted = true;
        const float direction = dx == 0 ? ((i ^ j) & 1 ? 1.0f : -1.0f)
          : (dx > 0 ? 1.0f : -1.0f);
        const float limit = min(0.6f, seconds * max(motion[i], motion[j]));
        const float aShare = motion[i] ? (motion[j] ? 0.5f : 1.0f) : 0.0f;
        const float bShare = motion[j] ? (motion[i] ? 0.5f : 1.0f) : 0.0f;
        a.x += direction * min(overlap * aShare, limit);
        b.x -= direction * min(overlap * bShare, limit);
        // Spacing should never silently wrap an old leaf into another lap.
        // The next wind step will renew it if it actually leaves the panel.
        a.x = max(0.0f, min(float(state.width) - 0.001f, a.x));
        b.x = max(0.0f, min(float(state.width) - 0.001f, b.x));
      }
    }
    if (!adjusted) break;
  }
}

void renderLeavesToSegment(Segment &seg) {
  seg.fill(BLACK);
  const uint16_t width = seg.vWidth(), height = seg.vHeight();
  if (!width || !height) return;
  if (seg.dataSize() != sizeof(LeavesState) && !seg.allocateData(sizeof(LeavesState))) return;
  auto &state = *reinterpret_cast<LeavesState *>(seg.data);
  if (state.magic != LEAVES_MAGIC || state.width != width || state.height != height || state.shedding != seg.check2) {
    memset(&state, 0, sizeof(state));
    state.magic = LEAVES_MAGIC;
    state.width = width; state.height = height; state.shedding = seg.check2;
    state.lastUpdate = strip.now;
    state.random = (strip.now ^ (uint32_t(width) << 16) ^ height) | 1U;
    state.spawnerCount = state.shedding ? 1 + leafRandom(state, LEAVES_SHED_CAPACITY) : 0;
    // A second spawner begins later, so two leaves do not move in lockstep.
    if (state.spawnerCount > 1) state.particles[1].wait = 12000 + leafRandom(state, 10001);
  }
  // Pauses/stalls must never cause a burst of accumulated falling events.
  const uint32_t dt = min<uint32_t>(100, strip.now - state.lastUpdate);
  state.lastUpdate = strip.now;
  // Sustained wind never pauses. The same front that kicks airborne particles
  // supplies extra ground speed at each leaf's absolute panel column.
  const float meanMotion = leafWindPixelsPerSecond(seg.speed);
  const float gustMotion = leafWindPixelsPerSecond(max(seg.speed, seg.custom2));
  const float direction = seg.check1 ? -1.0f : 1.0f;
  const float seconds = dt / 1000.0f;
  const unsigned capacity = state.shedding ? state.spawnerCount : LEAVES_CAPACITY;
  const float target = seg.intensity / 255.0f * min<float>(LEAVES_CAPACITY, float(width) * height / 8.0f);
  float motion[LEAVES_CAPACITY] = {};
  for (unsigned i = 0; i < capacity; i++) {
    LeafParticle &leaf = state.particles[i];
    const float visibility = state.shedding ? seg.intensity / 255.0f : max(0.0f, min(1.0f, target - i));
    bool spawnedNow = false;
    if (state.shedding && !leaf.active) {
      if (leaf.wait > dt) { leaf.wait -= dt; continue; }
      leaf.wait = 0;
      if (seg.custom1 > 0 && visibility > 0) spawnedNow = spawnShedLeaf(state, seg, leaf);
      if (!leaf.active) { leaf.wait = 12000 + leafRandom(state, 10001); continue; }
    }
    if (!state.shedding && !leaf.active && visibility > 0) spawnGroundLeaf(state, leaf, target);
    if (!leaf.active) continue;
    leaf.age = state.shedding ? leaf.age + (spawnedNow ? 0 : dt)
      : min<uint32_t>(15000, leaf.age + dt);
    if (state.shedding && leaf.stage == 1 && leaf.age >= LEAF_SPAWN_MS) {
      leaf.age = 0;
      leaf.stage = 2;
      leaf.falling = true;
    }
    if (state.shedding && leaf.stage == 2) {
      // Normal gravity covers the canopy-to-ground distance in two seconds.
      // Tree-fallen leaves keep their source column through landing and fade;
      // the separate ground-litter layer owns all wind-driven travel.
      const float progress = min(1.0f, leaf.age / float(LEAF_FALL_MS));
      leaf.y = leaf.sourceY + (height - 1 - leaf.sourceY) * progress * progress;
      if (leaf.age >= LEAF_FALL_MS) {
        leaf.y = height - 1;
        leaf.falling = false;
        leaf.stage = 3;
        leaf.age = 0;
      }
    } else if (!state.shedding || leaf.stage == 3) {
      if (!state.shedding) {
        const float pulse = gustMotion > meanMotion
          ? gustFieldStrength(leaf.x + seg.start, strip.now, seg.check1, seg.custom1) : 0.0f;
        const float slide = meanMotion + (gustMotion - meanMotion) * pulse;
        motion[i] = slide;
        // Identical wind response at the same X preserves the spacing between
        // leaves until the passing field compresses them; spacing is resolved
        // after all leaves have moved.
        leaf.x += seconds * direction * slide;
        if (leaf.x < 0 || leaf.x >= width)
          renewGroundLeaf(state, leaf, target, seg.check1);
      }
      if (state.shedding && leaf.age >= LEAF_DESPAWN_MS) {
        leaf.active = false;
        leaf.stage = 0;
        leaf.wait = 12000 + leafRandom(state, 10001);
        continue;
      }
    }
  }
  if (!state.shedding) spaceMovingGroundLeaves(state, target, motion, seconds);
  for (unsigned i = 0; i < capacity; i++) {
    const LeafParticle &leaf = state.particles[i];
    if (!leaf.active) continue;
    const float visibility = state.shedding ? seg.intensity / 255.0f
      : max(0.0f, min(1.0f, target - i));
    // The 10/2/10 second shed cycle changes phases; only the wait and source
    // positions vary. Ground litter fades in once and then stays put.
    const float fade = state.shedding ? (leaf.stage == 1 ? leaf.age / float(LEAF_SPAWN_MS)
      : leaf.stage == 2 ? 1.0f : 1.0f - leaf.age / float(LEAF_DESPAWN_MS))
      : leaf.age / 15000.0f;
    drawLeaf(seg, leaf.x, leaf.y, seg.colors[leaf.color], visibility * fade,
      !state.shedding);
  }
}
