#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32

#include <PNGdec.h>
#include <ctype.h>
#include <math.h>

namespace sprite_usermod {

#include "sprite_usermod_core.hpp"
#include "sprite_effect.hpp"
#include "precipitation_effect.hpp"
#include "dithered_gradient_effect.hpp"
#include "starfield_effect.hpp"

void addSpriteInfo(JsonObject &root) {
  JsonObject spriteInfo = root.createNestedObject(F("sprite_usermod"));
  spriteInfo[F("ready")] = spriteEffectId != 255;
  spriteInfo[F("fxid")] = spriteEffectId;
  spriteInfo[F("precip_fxid")] = precipitationEffectId;
  spriteInfo[F("dithered_gradient_fxid")] = ditheredGradientEffectId;
  spriteInfo[F("starfield_fxid")] = starfieldEffectId;
  spriteInfo[F("mode")] = F("cache");
  spriteInfo[F("budget")] = SPRITE_CACHE_BUDGET;
  spriteInfo[F("active")] = getActiveSpriteSegments();
  spriteInfo[F("bytes")] = getActiveSpriteBytes();
  spriteInfo[F("peak")] = spriteStats.peakCacheBytes;
  spriteInfo[F("loads")] = spriteStats.loads;
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
  spriteInfo[F("err")] = spriteStats.lastError;
}

void registerEffect() {
  if (spriteEffectId == 255) spriteEffectId = strip.addEffect(255, &modeSpriteUsermod, _data_FX_MODE_SPRITE);
  if (precipitationEffectId == 255) precipitationEffectId = strip.addEffect(255, &modePrecipitationUsermod, _data_FX_MODE_PRECIPITATION);
  if (ditheredGradientEffectId == 255) ditheredGradientEffectId = strip.addEffect(255, &modeDitheredGradientUsermod, _data_FX_MODE_DITHERED_GRADIENT);
  if (starfieldEffectId == 255) starfieldEffectId = strip.addEffect(255, &modeStarfieldUsermod, _data_FX_MODE_STARFIELD);
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
