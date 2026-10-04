#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32

#include <PNGdec.h>
#include <ctype.h>
#include <math.h>

namespace sprite_usermod {
// Whole-effect render times, kept per effect so one does not mask the other.
static uint32_t windDrawUs = 0, windMaxDrawUs = 0, leavesDrawUs = 0, leavesMaxDrawUs = 0;


#include "sprite_usermod_core.hpp"
#include "sprite_effect.hpp"
#include "precipitation_effect.hpp"
#include "dithered_gradient_effect.hpp"
#include "starfield_effect.hpp"
#include "wind_effect.hpp"
#include "leaves_effect.hpp"

// Optional per-segment gust ceiling in the same 0..255 scale as sx. Zero
// falls back to sx. Stored outside effect data so resizing preserves controls.
struct WindControls {
  GustTuning tuning;
  uint8_t gust = 0;
  uint8_t gustActivity = 0;
  int16_t spawnRateTenths = -1; // negative preserves the standalone density mode
  int16_t meanTenths = -1, gustTenths = -1; // physical m/s x10; negative = derive from sx
};
static WindControls windControls[MAX_NUM_SEGMENTS];

void readWindSegment(JsonObject elem, int id) {
  if (id < 0 || id >= strip.getSegmentsNum() || id >= MAX_NUM_SEGMENTS) return;
  if (strip.getSegment(id).mode != windEffectId && strip.getSegment(id).mode != leavesEffectId) {
    windControls[id] = WindControls{};
    return;
  }
  if (elem.containsKey("wind_gust_tuning")) {
    JsonObject obj = elem["wind_gust_tuning"];
    GustTuning &tuning = windControls[id].tuning;
    tuning.spacingPct = uint16_t(max(25, min(600, obj["spacing_pct"] | int(tuning.spacingPct))));
    tuning.durationPct = uint16_t(max(25, min(200, obj["duration_pct"] | int(tuning.durationPct))));
    tuning.attackMs = uint16_t(max(20, min(500, obj["attack_ms"] | int(tuning.attackMs))));
    tuning.roughnessPct = uint8_t(max(0, min(100, obj["roughness_pct"] | int(tuning.roughnessPct))));
    tuning.responseMs = uint16_t(max(30, min(1000, obj["response_ms"] | int(tuning.responseMs))));
    tuning.settleMs = uint16_t(max(50, min(2000, obj["settle_ms"] | int(tuning.settleMs))));
  } else if (elem.containsKey("fx")) windControls[id].tuning = GustTuning{};
  if (elem.containsKey("wind_mps10")) {
    JsonArray speeds = elem["wind_mps10"];
    const int mean = speeds[0] | -1, gust = speeds[1] | -1;
    windControls[id].meanTenths = int16_t(mean < 0 ? -1 : min(255, mean));
    windControls[id].gustTenths = int16_t(gust < 0 ? -1 : min(255, gust));
  } else if (elem.containsKey("fx")) {
    windControls[id].meanTenths = windControls[id].gustTenths = -1;
  }
  if (elem.containsKey("wind_gust")) {
    const int value = elem["wind_gust"] | 0;
    windControls[id].gust = uint8_t(max(0, min(255, value)));
  } else if (elem.containsKey("fx")) {
    // A full effect selection/preset without this extension means steady wind.
    windControls[id].gust = 0;
  }
  if (elem.containsKey("wind_gust_activity")) {
    const int value = elem["wind_gust_activity"] | 0;
    windControls[id].gustActivity = uint8_t(max(0, min(255, value)));
  } else if (elem.containsKey("fx")) {
    windControls[id].gustActivity = 0;
  }
  if (elem.containsKey("wind_spawn_rate")) {
    const float rate = elem["wind_spawn_rate"] | -1.0f;
    windControls[id].spawnRateTenths = !isfinite(rate) || rate < 0 ? -1 : int16_t(roundf(min(3.0f, rate) * 10.0f));
  } else if (elem.containsKey("fx")) {
    windControls[id].spawnRateTenths = -1;
  }
}

void readWindState(JsonObject &root) {
  if (root[F("rSeg")] | false) for (auto &controls : windControls) controls = WindControls{};
  JsonVariant segments = root["seg"];
  if (segments.is<JsonArray>()) {
    int index = 0;
    for (JsonObject elem : segments.as<JsonArray>()) {
      readWindSegment(elem, elem["id"] | index);
      index++;
    }
  } else if (segments.is<JsonObject>()) {
    JsonObject elem = segments.as<JsonObject>();
    const int id = elem["id"] | -1;
    if (id >= 0) readWindSegment(elem, id);
    else for (unsigned i = 0; i < strip.getSegmentsNum(); i++) {
      const Segment &seg = strip.getSegment(i);
      if (seg.isActive() && seg.isSelected()) readWindSegment(elem, i);
    }
  }
}

// WLED invokes this hook before serializing seg[], so report the extra control
// separately, indexed by segment id. Neoblaster folds it into the live segments.
void addWindState(JsonObject &root) {
  JsonArray gusts = root.createNestedArray("wind_gust");
  JsonArray activities = root.createNestedArray("wind_gust_activity");
  JsonArray rates = root.createNestedArray("wind_spawn_rate");
  JsonArray tunings = root.createNestedArray("wind_gust_tuning");
  JsonArray speeds = root.createNestedArray("wind_mps10");
  for (unsigned id = 0; id < strip.getSegmentsNum(); id++) {
    gusts.add(strip.getSegment(id).mode == windEffectId ? windControls[id].gust : 0);
    activities.add(strip.getSegment(id).mode == windEffectId ? windControls[id].gustActivity : 0);
    JsonObject obj = tunings.createNestedObject();
    const Segment &seg = strip.getSegment(id);
    if (seg.mode == windEffectId || (seg.mode == leavesEffectId && !seg.check2)) {
      const GustTuning &tuning = windControls[id].tuning;
      obj["spacing_pct"] = tuning.spacingPct;
      obj["duration_pct"] = tuning.durationPct;
      obj["attack_ms"] = tuning.attackMs;
      obj["roughness_pct"] = tuning.roughnessPct;
      obj["response_ms"] = tuning.responseMs;
      obj["settle_ms"] = tuning.settleMs;
    }
    JsonArray speed = speeds.createNestedArray();
    if (seg.mode == windEffectId && windControls[id].meanTenths >= 0) {
      speed.add(windControls[id].meanTenths);
      speed.add(windControls[id].gustTenths);
    }
    const int16_t rate = windControls[id].spawnRateTenths;
    rates.add(strip.getSegment(id).mode == windEffectId && rate >= 0 ? rate / 10.0f : -1.0f);
  }
}

void addSpriteInfo(JsonObject &root) {
  JsonObject spriteInfo = root.createNestedObject(F("sprite_usermod"));
  spriteInfo[F("ready")] = spriteEffectId != 255;
  spriteInfo[F("fxid")] = spriteEffectId;
  spriteInfo[F("precip_fxid")] = precipitationEffectId;
  spriteInfo[F("dithered_gradient_fxid")] = ditheredGradientEffectId;
  spriteInfo[F("starfield_fxid")] = starfieldEffectId;
  spriteInfo[F("wind_fxid")] = windEffectId;
  spriteInfo[F("leaves_fxid")] = leavesEffectId;
  spriteInfo[F("mode")] = F("cache");
  spriteInfo[F("cache_generation")] = spriteCacheGeneration;
  spriteInfo[F("budget")] = SPRITE_CACHE_BUDGET;
  spriteInfo[F("active")] = getActiveSpriteSegments();
  spriteInfo[F("bytes")] = getActiveSpriteBytes();
  spriteInfo[F("peak")] = spriteStats.peakCacheBytes;
  spriteInfo[F("loads")] = spriteStats.loads;
  spriteInfo[F("clones")] = spriteStats.cacheClones;
  spriteInfo[F("shared_hits")] = spriteStats.sharedCacheHits;
  spriteInfo[F("errors")] = spriteStats.loadErrors;
  spriteInfo[F("loadms")] = spriteStats.lastLoadMs;
  spriteInfo[F("frames")] = spriteStats.lastFrames;
  spriteInfo[F("w")] = spriteStats.lastWidth;
  spriteInfo[F("h")] = spriteStats.lastHeight;
  spriteInfo[F("draws")] = spriteStats.draws;
  spriteInfo[F("skips")] = spriteStats.skips;
  spriteInfo[F("pixels")] = spriteStats.blitPixels;
  spriteInfo[F("drawus")] = spriteStats.lastDrawUs;
  spriteInfo[F("drawmax")] = spriteStats.maxDrawUs;
  spriteInfo[F("wind_us")] = windDrawUs;
  spriteInfo[F("wind_max_us")] = windMaxDrawUs;
  spriteInfo[F("leaves_us")] = leavesDrawUs;
  spriteInfo[F("leaves_max_us")] = leavesMaxDrawUs;
  spriteInfo[F("flow_grid")] = "16x8";
  spriteInfo[F("leaf_lifts")] = leafExchange.liftsTaken;
  spriteInfo[F("leaf_landings")] = leafExchange.landingsTaken;
  spriteInfo[F("err")] = spriteStats.lastError;
}

void readSpriteState(JsonObject &root) {
  JsonObject spriteState = root[F("sprite_usermod")];
  if (spriteState.isNull()) return;

  if (spriteState[F("clear_cache")] | false) {
    bumpSpriteCacheGeneration();
    return;
  }

  JsonVariant epoch = spriteState[F("asset_epoch")];
  if (!epoch.isNull()) {
    setSpriteCacheGeneration(epoch.as<uint32_t>());
  }
}

void registerEffect() {
  if (spriteEffectId == 255) spriteEffectId = strip.addEffect(255, &modeSpriteUsermod, _data_FX_MODE_SPRITE);
  if (precipitationEffectId == 255) precipitationEffectId = strip.addEffect(255, &modePrecipitationUsermod, _data_FX_MODE_PRECIPITATION);
  if (ditheredGradientEffectId == 255) ditheredGradientEffectId = strip.addEffect(255, &modeDitheredGradientUsermod, _data_FX_MODE_DITHERED_GRADIENT);
  if (starfieldEffectId == 255) starfieldEffectId = strip.addEffect(255, &modeStarfieldUsermod, _data_FX_MODE_STARFIELD);
  if (windEffectId == 255) windEffectId = strip.addEffect(255, &modeWindUsermod, _data_FX_MODE_WIND);
  if (leavesEffectId == 255) leavesEffectId = strip.addEffect(255, &modeLeavesUsermod, _data_FX_MODE_LEAVES);
}

void modeSpriteUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }
  if (SEGENV.call == 0) resetSpriteRuntime(SEGMENT);
  if (renderUsermodSpriteToSegment(SEGMENT) != SPRITE_ERROR_NONE) SEGMENT.fill(SEGCOLOR(0));
}

void modePrecipitationUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }
  renderPrecipitationToSegment(SEGMENT);
}

void modeDitheredGradientUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }
  renderDitheredGradientToSegment(SEGMENT);
}

void modeStarfieldUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }
  renderStarfieldToSegment(SEGMENT);
}

void modeWindUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(SEGCOLOR(0));
    return;
  }
  const uint32_t start = micros();
  const WindControls &controls = windControls[strip.getCurrSegmentId()];
  renderWindToSegment(SEGMENT, controls.gust, controls.spawnRateTenths, controls.gustActivity,
                      controls.tuning, controls.meanTenths, controls.gustTenths);
  windDrawUs = micros() - start;
  windMaxDrawUs = max(windMaxDrawUs, windDrawUs);
}

void modeLeavesUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(BLACK);
    return;
  }
  const uint32_t start = micros();
  renderLeavesToSegment(SEGMENT, windControls[strip.getCurrSegmentId()].tuning);
  // Only the ground instance runs the fluid solver; shedding would mask it.
  if (!SEGMENT.check2) {
    leavesDrawUs = micros() - start;
    leavesMaxDrawUs = max(leavesMaxDrawUs, leavesDrawUs);
  }
}

} // namespace sprite_usermod

class SpriteUsermod : public Usermod {
  public:
    void setup() override {
      sprite_usermod::registerEffect();
    }

    void loop() override {}

    void addToJsonInfo(JsonObject &root) override {
      sprite_usermod::addSpriteInfo(root);
    }

    void addToJsonState(JsonObject &root) override {
      sprite_usermod::addWindState(root);
    }

    void readFromJsonState(JsonObject &root) override {
      sprite_usermod::readSpriteState(root);
      sprite_usermod::readWindState(root);
    }
};

static SpriteUsermod spriteUsermod;
REGISTER_USERMOD(spriteUsermod);

#else

#include "wled.h"

class SpriteUsermod : public Usermod {
  public:
    void setup() override {}
    void loop() override {}
};

static SpriteUsermod spriteUsermod;
REGISTER_USERMOD(spriteUsermod);

#endif
