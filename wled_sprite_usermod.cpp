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
#include "cloud_deck_effect.hpp"

// Wind's own spawn rate, per segment. Stored outside effect data so resizing
// preserves it. Everything about the air itself is panel-wide: see panel_wind.hpp.
struct WindControls {
  int16_t spawnRateTenths = -1; // negative preserves the standalone density mode
};
static WindControls windControls[MAX_NUM_SEGMENTS];

void readWindSegment(JsonObject elem, int id) {
  if (id < 0 || id >= strip.getSegmentsNum() || id >= MAX_NUM_SEGMENTS) return;
  if (strip.getSegment(id).mode != windEffectId) {
    windControls[id] = WindControls{};
    return;
  }
  if (elem.containsKey("wind_spawn_rate")) {
    const float rate = elem["wind_spawn_rate"] | -1.0f;
    windControls[id].spawnRateTenths = !isfinite(rate) || rate < 0 ? -1 : int16_t(roundf(min(3.0f, rate) * 10.0f));
  } else if (elem.containsKey("fx")) {
    windControls[id].spawnRateTenths = -1;
  }
}

// Partial updates merge into what the host sent before. `false` hands the
// wind back to each segment's own controls.
void readPanelWind(JsonVariant value) {
  if (value.isNull()) return;
  PanelWind &wind = panelWind;
  if (value.is<bool>()) {
    if (!value.as<bool>()) wind = PanelWind{};
    return;
  }
  if (!value.is<JsonObject>()) return;
  JsonObject obj = value.as<JsonObject>();
  const auto clampByte = [](int v) { return uint8_t(max(0, min(255, v))); };
  if (obj.containsKey("mps10")) {
    JsonArray speeds = obj["mps10"];
    wind.meanTenths = clampByte(speeds[0] | int(wind.meanTenths));
    wind.gustTenths = clampByte(speeds[1] | int(wind.gustTenths));
  }
  if (obj.containsKey("air")) {
    JsonArray speeds = obj["air"];
    wind.airMean = clampByte(speeds[0] | int(wind.airMean));
    wind.airGust = clampByte(speeds[1] | int(wind.airGust));
  }
  if (obj.containsKey("activity")) wind.activity = clampByte(obj["activity"] | int(wind.activity));
  if (obj.containsKey("turbulence")) wind.turbulence = clampByte(obj["turbulence"] | int(wind.turbulence));
  if (obj.containsKey("left")) wind.blowLeft = obj["left"] | wind.blowLeft;
  if (obj.containsKey("gust_tuning")) {
    JsonObject t = obj["gust_tuning"];
    GustTuning &tuning = wind.tuning;
    tuning.spacingPct = uint16_t(max(25, min(600, t["spacing_pct"] | int(tuning.spacingPct))));
    tuning.durationPct = uint16_t(max(25, min(200, t["duration_pct"] | int(tuning.durationPct))));
    tuning.attackMs = uint16_t(max(20, min(500, t["attack_ms"] | int(tuning.attackMs))));
    tuning.roughnessPct = uint8_t(max(0, min(100, t["roughness_pct"] | int(tuning.roughnessPct))));
    tuning.responseMs = uint16_t(max(30, min(1000, t["response_ms"] | int(tuning.responseMs))));
    tuning.settleMs = uint16_t(max(50, min(2000, t["settle_ms"] | int(tuning.settleMs))));
  }
  wind.valid = true;
}

void readWindState(JsonObject &root) {
  readPanelWind(root["panel_wind"]);
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

// WLED invokes this hook before serializing seg[], so the per-segment spawn
// rate is reported as a top-level array indexed by segment id. Neoblaster
// folds it into the live segments.
void addWindState(JsonObject &root) {
  if (panelWind.valid) {
    JsonObject wind = root.createNestedObject("panel_wind");
    JsonArray mps = wind.createNestedArray("mps10");
    mps.add(panelWind.meanTenths);
    mps.add(panelWind.gustTenths);
    JsonArray air = wind.createNestedArray("air");
    air.add(panelWind.airMean);
    air.add(panelWind.airGust);
    wind["activity"] = panelWind.activity;
    wind["turbulence"] = panelWind.turbulence;
    wind["left"] = panelWind.blowLeft;
    JsonObject tuning = wind.createNestedObject("gust_tuning");
    tuning["spacing_pct"] = panelWind.tuning.spacingPct;
    tuning["duration_pct"] = panelWind.tuning.durationPct;
    tuning["attack_ms"] = panelWind.tuning.attackMs;
    tuning["roughness_pct"] = panelWind.tuning.roughnessPct;
    tuning["response_ms"] = panelWind.tuning.responseMs;
    tuning["settle_ms"] = panelWind.tuning.settleMs;
  }
  JsonArray rates = root.createNestedArray("wind_spawn_rate");
  for (unsigned id = 0; id < strip.getSegmentsNum(); id++) {
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
  spriteInfo[F("cloud_deck_fxid")] = cloudDeckEffectId;
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
  spriteInfo[F("panel_wind")] = panelWind.valid;
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
  if (cloudDeckEffectId == 255) cloudDeckEffectId = strip.addEffect(255, &modeCloudDeckUsermod, _data_FX_MODE_CLOUD_DECK);
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
  const WindInputs wind = panelWindInputs();
  renderPrecipitationToSegment(SEGMENT, panelWind.valid ? &wind : nullptr);
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
  const WindInputs wind = panelWind.valid ? panelWindInputs() : windInputsFromSegment(SEGMENT);
  renderWindToSegment(SEGMENT, wind, windControls[strip.getCurrSegmentId()].spawnRateTenths);
  windDrawUs = micros() - start;
  windMaxDrawUs = max(windMaxDrawUs, windDrawUs);
}

void modeLeavesUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(BLACK);
    return;
  }
  const uint32_t start = micros();
  renderLeavesToSegment(SEGMENT, panelWind.valid ? panelWindInputs() : leafWindFromSegment(SEGMENT));
  // Only the ground instance runs the fluid solver; shedding would mask it.
  if (!SEGMENT.check2) {
    leavesDrawUs = micros() - start;
    leavesMaxDrawUs = max(leavesMaxDrawUs, leavesDrawUs);
  }
}

void modeCloudDeckUsermod() {
  if (!strip.isMatrix || !SEGMENT.is2D()) {
    SEGMENT.fill(BLACK);
    return;
  }
  const WindInputs wind = panelWindInputs();
  renderCloudDeckToSegment(SEGMENT, panelWind.valid ? &wind : nullptr);
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
