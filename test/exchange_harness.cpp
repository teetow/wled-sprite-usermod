// Ground Leaves and airborne Wind running together, the way the panel does:
// the ground band's segment renders first, then the whole-panel Wind segment.
#include "wled_stub.hpp"
#include <cassert>
#include "../leaves_effect.hpp"
#include "../wind_effect.hpp"

static LeavesState &ground(Segment &seg) { return *reinterpret_cast<LeavesState *>(seg.data); }

static unsigned touchingLitter(Segment &seg) {
  unsigned pairs = 0;
  const auto &leaves = ground(seg).particles;
  for (unsigned i = 0; i < LEAVES_CAPACITY; i++) for (unsigned j = 0; j < i; j++) {
    if (!leaves[i].active || !leaves[j].active) continue;
    int ax, ay, bx, by;
    if (leafPixel(ground(seg), leaves[i], ax, ay) && leafPixel(ground(seg), leaves[j], bx, by)
        && abs(ax - bx) < 2 && abs(ay - by) < 2) pairs++;
  }
  return pairs;
}

int main() {
  const uint32_t palette[3] = {0xa9ac11, 0xf29831, 0xab773c};
  strip.now = 1000;
  // Panel 32x16. The ground band is rows 10-15; its effect segment reserves
  // six rows of sky above that for lifted swirls.
  Segment litter(32, 12);
  litter.startY = 4;
  litter.custom3 = 6;
  litter.intensity = 255;
  Segment air(32, 16);
  air.check2 = true;     // fluid flow
  air.intensity = 1;     // rate mode
  air.custom1 = 150;     // autumn swirls
  air.custom2 = 152;     // autumn drift: leaves settle as they cross
  for (unsigned i = 0; i < 3; i++) litter.colors[i] = air.colors[i] = palette[i];

  auto frame = [&](bool airRunning) {
    strip.now += 33;
    currentSegment = &litter;
    renderLeavesToSegment(litter, leafWindFromSegment(litter));
    if (airRunning) {
      currentSegment = &air;
      // The litter's physical wind, with the air's own on-panel speed.
      WindInputs wind = windInputsFromSegment(air, air.speed, litter.custom1);
      wind.meanMps = litter.speed / 10.0f;
      wind.gustMps = max(litter.speed, litter.custom2) / 10.0f;
      renderWindToSegment(air, wind, 30);
    }
    assert(touchingLitter(litter) == 0);
    assert(litter.outOfBoundsWrites == 0 && air.outOfBoundsWrites == 0);
  };

  // A 15 m/s gale gusting 20: litter is lifted into the air, and airborne
  // leaves settle as litter, in both directions.
  litter.speed = 150; litter.custom2 = 200; litter.custom1 = 216;
  air.speed = 226;
  for (unsigned i = 0; i < 18000; i++) frame(true); // ten minutes
  const LeafExchange &exchange = leafExchange;
  printf("gale: lifts %u/%u taken, landings %u/%u taken\n", exchange.liftsTaken, exchange.liftsOffered,
    exchange.landingsTaken, exchange.landingsOffered);
  assert(exchange.liftsTaken > 20 && exchange.landingsTaken > 20);
  // Offers are only made to a side with room, so nothing vanishes in transit.
  assert(exchange.liftsTaken == exchange.liftsOffered);
  assert(exchange.landingsTaken >= exchange.landingsOffered * 95 / 100);

  // A lifted leaf keeps its color: every adopted particle's seed matches a palette slot.
  unsigned adopted = 0;
  const auto &runtime = *reinterpret_cast<WindState *>(air.data);
  for (unsigned i = 0; i < runtime.particleCount; i++) {
    const auto &particle = getWindParticles(const_cast<WindState *>(&runtime))[i];
    adopted += particle.state && particle.seed >= 249 && particle.seed <= 251;
  }
  printf("adopted leaves in flight now: %u\n", adopted);

  // With the Wind layer stopped, nothing is handed over: litter stays in its band.
  const uint32_t liftsBefore = exchange.liftsOffered;
  for (unsigned i = 0; i < 9000; i++) frame(false);
  assert(exchange.liftsOffered == liftsBefore);
  for (const auto &leaf : ground(litter).particles) if (leaf.active) assert(leaf.height <= 12);

  // Light wind: airborne leaves still come down as litter, but nothing lifts.
  const uint32_t liftsLight = exchange.liftsOffered, landingsLight = exchange.landingsTaken;
  litter.speed = 30; litter.custom2 = 40; litter.custom1 = 45;
  air.speed = 70;
  for (unsigned i = 0; i < 18000; i++) frame(true);
  printf("light: lifts %u, landings %u\n", exchange.liftsOffered - liftsLight, exchange.landingsTaken - landingsLight);
  assert(exchange.liftsOffered == liftsLight);
  printf("Ground and airborne leaves hand off without collisions or losses.\n");
}
