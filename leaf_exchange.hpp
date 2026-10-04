#pragma once
// Hand-off between the ground Leaves effect and the airborne Wind effect.
// Both run inside this one usermod, so a small static buffer lets a ground
// leaf lifted clear of the horizon continue as an airborne particle, and lets
// an airborne leaf settle as litter. Positions are absolute panel pixels; each
// effect converts with its own segment offset. Each side also publishes a
// heartbeat and how much room it has, so nothing is handed to an effect that
// is not running or has no free slot, and nothing silently vanishes.

constexpr unsigned LEAF_EXCHANGE_SLOTS = 8;
constexpr uint32_t LEAF_EXCHANGE_TTL_MS = 250;
constexpr unsigned LEAF_EXCHANGE_MAX_ROWS = 32;
constexpr unsigned LEAF_EXCHANGE_MAX_COLUMNS = 64;

struct LeafHandoff {
  float x, y;   // absolute panel pixels
  float vx, vy; // screen pixels per second
  uint8_t color; // index into the shared three-color palette
  uint32_t postedAt;
  bool pending;
};

struct LeafExchange {
  LeafHandoff toAir[LEAF_EXCHANGE_SLOTS], toGround[LEAF_EXCHANGE_SLOTS];
  uint32_t airSeenAt, groundSeenAt;
  bool airSeen, groundSeen;
  uint8_t airRoom, groundRoom;
  // The ground band in absolute panel coordinates: rows [groundTop, groundBottom),
  // columns [groundLeft, groundRight), and which LEDs its litter lights.
  int16_t groundTop, groundBottom, groundLeft, groundRight;
  uint64_t groundLit[LEAF_EXCHANGE_MAX_ROWS];
  // Running totals, reported in /json/info: hand-offs offered and accepted.
  uint32_t liftsOffered, liftsTaken, landingsOffered, landingsTaken;
};

static LeafExchange leafExchange;

inline bool leafExchangeAlive(bool seen, uint32_t seenAt) {
  return seen && strip.now - seenAt < LEAF_EXCHANGE_TTL_MS;
}

inline bool leafHandoffLive(const LeafHandoff &handoff) {
  return handoff.pending && strip.now - handoff.postedAt < LEAF_EXCHANGE_TTL_MS;
}

inline bool postLeafHandoff(LeafHandoff *queue, float x, float y, float vx, float vy, uint8_t color) {
  for (unsigned i = 0; i < LEAF_EXCHANGE_SLOTS; i++) {
    if (leafHandoffLive(queue[i])) continue;
    queue[i] = {x, y, vx, vy, color, strip.now, true};
    return true;
  }
  return false;
}

// Whether a leaf landing on this absolute LED would touch existing litter.
inline bool groundLandingFree(int x, int y) {
  const LeafExchange &exchange = leafExchange;
  if (y < exchange.groundTop || y >= exchange.groundBottom) return false;
  if (x < exchange.groundLeft || x >= exchange.groundRight || x < 0 || x >= int(LEAF_EXCHANGE_MAX_COLUMNS)) return false;
  for (int row = y - 1; row <= y + 1; row++) {
    if (row < 0 || row >= int(LEAF_EXCHANGE_MAX_ROWS)) continue;
    const uint64_t lit = exchange.groundLit[row];
    for (int column = x - 1; column <= x + 1; column++) {
      if (column >= 0 && column < int(LEAF_EXCHANGE_MAX_COLUMNS) && (lit >> column & 1U)) return false;
    }
  }
  return true;
}
