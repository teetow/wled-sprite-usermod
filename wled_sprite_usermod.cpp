#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32

#include <PNGdec.h>
#include <ctype.h>
#include <math.h>

namespace sprite_usermod {

constexpr uint32_t SPRITE_STATE_MAGIC = 0x53505254UL;
constexpr uint16_t SPRITE_MAX_DIMENSION = 320;
constexpr uint16_t SPRITE_MAX_INSTANCES = 64;
constexpr size_t SPRITE_CACHE_BUDGET = (MAX_SEGMENT_DATA > (12 * 1024)) ? (12 * 1024) : MAX_SEGMENT_DATA;
constexpr uint32_t PRECIPITATION_STATE_MAGIC = 0x50524351UL;
constexpr uint16_t PRECIPITATION_MAX_PARTICLES = 192;
constexpr uint8_t PRECIPITATION_FP_SHIFT = 8;
constexpr int16_t PRECIPITATION_FP_SCALE = 1 << PRECIPITATION_FP_SHIFT;
constexpr uint8_t PRECIPITATION_SPLASH_TIME_SHIFT = 3;
constexpr uint8_t PRECIPITATION_SPLASH_TIME_SCALE = 1 << PRECIPITATION_SPLASH_TIME_SHIFT;
constexpr uint16_t PRECIPITATION_SPLASH_FRAME_MS = 1000 / 30;
constexpr uint8_t PRECIPITATION_REFERENCE_FALL = 34;
constexpr float DITHERED_GRADIENT_TWO_PI = 6.28318530718f;
constexpr float STARFIELD_DEFAULT_RATE = 1.6f;
constexpr float STARFIELD_MIN_FADE = 0.18f;
constexpr float STARFIELD_FADE_SPAN = 0.82f;
constexpr byte SPRITE_ERROR_NONE = 0;
constexpr byte SPRITE_ERROR_NO_NAME = 1;
constexpr byte SPRITE_ERROR_UNSUPPORTED = 2;
constexpr byte SPRITE_ERROR_FILE_MISSING = 3;
constexpr byte SPRITE_ERROR_BAD_ANIMDEF = 4;
constexpr byte SPRITE_ERROR_ALLOC = 5;
constexpr byte SPRITE_ERROR_PNG = 6;
constexpr byte SPRITE_ERROR_TOO_LARGE = 7;

struct SpriteFrameSpec {
  uint16_t durationMs;
  uint16_t width;
  uint16_t height;
  char filename[WLED_MAX_SEGNAME_LEN + 2];
};

struct SpriteFrame {
  uint16_t durationMs;
  uint16_t width;
  uint16_t height;
  uint32_t dataOffset;
};

struct SpritePixel {
  uint16_t color565;
  uint8_t alpha;
} __attribute__((packed));

struct SpriteStats {
  uint32_t loads;
  uint32_t loadErrors;
  uint32_t lastLoadMs;
  uint32_t peakCacheBytes;
  uint32_t draws;
  uint32_t skips;
  uint32_t blitPixels;
  uint32_t lastDrawUs;
  uint32_t maxDrawUs;
  uint16_t lastWidth;
  uint16_t lastHeight;
  uint16_t lastFrames;
  uint8_t lastError;
};

struct SpriteInstance {
  int16_t x;
  int16_t y;
};

struct SpriteState {
  uint32_t magic;
  char assetName[WLED_MAX_SEGNAME_LEN + 2];
  uint16_t frameCount;
  uint16_t currentFrame;
  uint32_t nextFrameTime;
  uint32_t pixelDataOffset;
  uint32_t cacheBytes;
  uint32_t layoutKey;
  uint32_t lastRenderKey;
  uint16_t maxWidth;
  uint16_t maxHeight;
  uint16_t instanceCount;
  uint8_t lastError;
};

struct SpriteRenderContext {
  Segment *segment;
  uint16_t segmentWidth;
  uint16_t segmentHeight;
  uint16_t spriteWidth;
  uint16_t spriteHeight;
  uint16_t motionOffset;
  uint16_t instanceCount;
  bool useMask;
  bool useGradient;
  bool useHorizontalGradient;
  bool moving;
  const SpriteInstance *instances;
};

struct SpriteCacheLoadContext {
  SpritePixel *pixels;
  uint16_t width;
  uint16_t height;
};

struct PrecipitationParticle {
  int32_t x;
  int32_t y;
  int16_t vx;
  int16_t vy;
  uint8_t state;
  uint8_t age;
  uint8_t ttl;
  uint8_t seed;
} __attribute__((packed));

struct PrecipitationState {
  uint32_t magic;
  uint32_t lastUpdate;
  uint32_t prngState;
  uint16_t width;
  uint16_t height;
  uint16_t particleCount;
  uint16_t activeTarget;
};

class SpritePrng {
  public:
    explicit SpritePrng(uint32_t seed) : _state(seed ? seed : 0x9E3779B9UL) {}

    uint16_t random16(uint16_t upperExclusive) {
      if (upperExclusive <= 1) return 0;
      _state ^= _state << 13;
      _state ^= _state >> 17;
      _state ^= _state << 5;
      return uint16_t(_state % upperExclusive);
    }

  private:
    uint32_t _state;
};

static File spritePngFile;
static SpriteStats spriteStats = {};
static uint8_t spriteEffectId = 255;
static uint8_t precipitationEffectId = 255;
static uint8_t ditheredGradientEffectId = 255;
static uint8_t starfieldEffectId = 255;

static const char _data_FX_MODE_SPRITE[] PROGMEM = "Sprite@Move speed,Instances,,,,Mask,Gradient,Horizontal Gradient;;!;2;sx=0,ix=0";
static const char _data_FX_MODE_PRECIPITATION[] PROGMEM = "Precipitation@Fall speed,Density,Bounciness,Shear,Trail length;Particle,Trail start,Trail end;!;2;sx=128,ix=128,c1=128,c2=128,c3=10";
static const char _data_FX_MODE_DITHERED_GRADIENT[] PROGMEM = "DitheredGradient@Dither,Rotation;;!;2;sx=0,ix=0";
static const char _data_FX_MODE_STARFIELD[] PROGMEM = "Starfield@Twinkle speed,Density,,,,Gradient;;!;2;sx=128,ix=160";

void modeSpriteUsermod();
void modePrecipitationUsermod();
void modeDitheredGradientUsermod();
void modeStarfieldUsermod();
void *spritePngOpen(const char *filename, int32_t *size);
void spritePngClose(void *handle);
int32_t spritePngRead(PNGFILE *handle, uint8_t *buffer, int32_t length);
int32_t spritePngSeek(PNGFILE *handle, int32_t position);
int spriteCacheWrite(PNGDRAW *pDraw);

inline SpriteState *getSpriteState(Segment &seg) {
  if (!seg.data) return nullptr;
  auto *state = reinterpret_cast<SpriteState *>(seg.data);
  return (state->magic == SPRITE_STATE_MAGIC) ? state : nullptr;
}

inline const SpriteState *getSpriteState(const Segment &seg) {
  if (!seg.data) return nullptr;
  auto *state = reinterpret_cast<const SpriteState *>(seg.data);
  return (state->magic == SPRITE_STATE_MAGIC) ? state : nullptr;
}

inline SpriteFrame *getSpriteFrames(SpriteState *state) {
  return reinterpret_cast<SpriteFrame *>(state + 1);
}

inline const SpriteFrame *getSpriteFrames(const SpriteState *state) {
  return reinterpret_cast<const SpriteFrame *>(state + 1);
}

inline SpriteInstance *getSpriteInstances(SpriteState *state) {
  return reinterpret_cast<SpriteInstance *>(getSpriteFrames(state) + state->frameCount);
}

inline const SpriteInstance *getSpriteInstances(const SpriteState *state) {
  return reinterpret_cast<const SpriteInstance *>(getSpriteFrames(state) + state->frameCount);
}

inline uint8_t *getSpritePixelBlob(SpriteState *state) {
  return reinterpret_cast<uint8_t *>(state) + state->pixelDataOffset;
}

inline const uint8_t *getSpritePixelBlob(const SpriteState *state) {
  return reinterpret_cast<const uint8_t *>(state) + state->pixelDataOffset;
}

inline PrecipitationParticle *getPrecipitationParticles(PrecipitationState *state) {
  return reinterpret_cast<PrecipitationParticle *>(state + 1);
}

bool endsWithIgnoreCase(const char *value, const char *suffix) {
  if (!value || !suffix) return false;
  const size_t valueLen = strlen(value);
  const size_t suffixLen = strlen(suffix);
  if (suffixLen > valueLen) return false;
  value += valueLen - suffixLen;
  for (size_t index = 0; index < suffixLen; index++) {
    if (tolower(value[index]) != tolower(suffix[index])) return false;
  }
  return true;
}

void normalizeRootPath(const char *name, char (&buffer)[WLED_MAX_SEGNAME_LEN + 2]) {
  if (!name || name[0] == '\0') {
    buffer[0] = '\0';
    return;
  }
  if (name[0] == '/') {
    strncpy(buffer, name, WLED_MAX_SEGNAME_LEN + 1);
    buffer[WLED_MAX_SEGNAME_LEN + 1] = '\0';
    return;
  }
  buffer[0] = '/';
  strncpy(buffer + 1, name, WLED_MAX_SEGNAME_LEN);
  buffer[WLED_MAX_SEGNAME_LEN + 1] = '\0';
}

void resolveFramePath(const char *assetPath, const char *filename, char (&buffer)[WLED_MAX_SEGNAME_LEN + 2]) {
  if (!filename || filename[0] == '\0') {
    buffer[0] = '\0';
    return;
  }

  if (filename[0] == '/') {
    strncpy(buffer, filename, WLED_MAX_SEGNAME_LEN + 1);
    buffer[WLED_MAX_SEGNAME_LEN + 1] = '\0';
    return;
  }

  const char *lastSlash = strrchr(assetPath, '/');
  size_t prefixLen = (lastSlash && lastSlash != assetPath) ? size_t(lastSlash - assetPath + 1) : 1U;
  if (prefixLen > WLED_MAX_SEGNAME_LEN + 1) prefixLen = WLED_MAX_SEGNAME_LEN + 1;
  memcpy(buffer, assetPath, prefixLen);
  buffer[prefixLen] = '\0';
  strncat(buffer, filename, WLED_MAX_SEGNAME_LEN + 1 - prefixLen);
}

uint32_t hashString32(const char *value) {
  uint32_t hash = 2166136261UL;
  while (value && *value) {
    hash ^= uint8_t(*value++);
    hash *= 16777619UL;
  }
  return hash;
}

uint8_t unpackPackedPixel(const uint8_t *source, uint16_t pixelIndex, uint8_t bitsPerPixel) {
  if (bitsPerPixel >= 8) return source[pixelIndex];
  const uint8_t mask = (1U << bitsPerPixel) - 1U;
  const uint8_t pixelsPerByte = 8U / bitsPerPixel;
  const uint8_t byteValue = source[pixelIndex / pixelsPerByte];
  const uint8_t shift = 8U - bitsPerPixel * ((pixelIndex % pixelsPerByte) + 1U);
  return (byteValue >> shift) & mask;
}

uint32_t rgbToRgb32(uint8_t red, uint8_t green, uint8_t blue) {
  return RGBW32(red, green, blue, 0);
}

uint16_t rgb32To565(uint32_t color) {
  return uint16_t(((R(color) & 0xF8) << 8) | ((G(color) & 0xFC) << 3) | (B(color) >> 3));
}

uint32_t rgb565ToRgb32(uint16_t color) {
  const uint8_t red = ((color >> 11) & 0x1F) * 255 / 31;
  const uint8_t green = ((color >> 5) & 0x3F) * 255 / 63;
  const uint8_t blue = (color & 0x1F) * 255 / 31;
  return rgbToRgb32(red, green, blue);
}

bool decodeSpritePixel(const PNGDRAW *pDraw, uint16_t pixelX, uint32_t &color, uint8_t &alpha) {
  const uint8_t *pixels = pDraw->pPixels;
  switch (pDraw->iPixelType) {
    case PNG_PIXEL_TRUECOLOR: {
      const uint8_t *pixel = pixels + pixelX * 3U;
      color = rgbToRgb32(pixel[0], pixel[1], pixel[2]);
      alpha = 255;
      return true;
    }
    case PNG_PIXEL_TRUECOLOR_ALPHA: {
      const uint8_t *pixel = pixels + pixelX * 4U;
      color = rgbToRgb32(pixel[0], pixel[1], pixel[2]);
      alpha = pixel[3];
      return true;
    }
    case PNG_PIXEL_GRAYSCALE: {
      const uint8_t value = unpackPackedPixel(pixels, pixelX, pDraw->iBpp);
      const uint16_t levels = (1U << pDraw->iBpp) - 1U;
      const uint8_t gray = levels ? uint8_t((uint16_t(value) * 255U) / levels) : 0;
      color = rgbToRgb32(gray, gray, gray);
      alpha = 255;
      return true;
    }
    case PNG_PIXEL_GRAY_ALPHA: {
      const uint8_t *pixel = pixels + pixelX * 2U;
      color = rgbToRgb32(pixel[0], pixel[0], pixel[0]);
      alpha = pixel[1];
      return true;
    }
    case PNG_PIXEL_INDEXED: {
      const uint8_t index = unpackPackedPixel(pixels, pixelX, pDraw->iBpp);
      const uint8_t *palette = &pDraw->pPalette[index * 3U];
      color = rgbToRgb32(palette[0], palette[1], palette[2]);
      alpha = pDraw->iHasAlpha ? pDraw->pPalette[768 + index] : 255;
      return true;
    }
    default:
      color = 0;
      alpha = 0;
      return false;
  }
}

void noteSpriteLoad(uint16_t width, uint16_t height, uint16_t frames, uint32_t cacheBytes, uint32_t loadMs) {
  spriteStats.loads++;
  spriteStats.lastLoadMs = loadMs;
  spriteStats.lastWidth = width;
  spriteStats.lastHeight = height;
  spriteStats.lastFrames = frames;
  if (cacheBytes > spriteStats.peakCacheBytes) spriteStats.peakCacheBytes = cacheBytes;
  spriteStats.lastError = SPRITE_ERROR_NONE;
}

void noteSpriteError(byte error) {
  if (error != SPRITE_ERROR_NONE) spriteStats.loadErrors++;
  spriteStats.lastError = error;
}

void noteSpriteDraw(uint32_t drawUs, uint32_t blitPixels, bool skipped) {
  if (skipped) {
    spriteStats.skips++;
    return;
  }
  spriteStats.draws++;
  spriteStats.blitPixels += blitPixels;
  spriteStats.lastDrawUs = drawUs;
  if (drawUs > spriteStats.maxDrawUs) spriteStats.maxDrawUs = drawUs;
}

size_t getActiveSpriteBytes() {
  if (spriteEffectId == 255) return 0;
  size_t activeBytes = 0;
  for (size_t index = 0; index < strip.getSegmentsNum(); index++) {
    const Segment &segment = strip.getSegment(index);
    if (segment.isActive() && segment.mode == spriteEffectId) activeBytes += segment.dataSize();
  }
  return activeBytes;
}

uint8_t getActiveSpriteSegments() {
  if (spriteEffectId == 255) return 0;
  uint8_t activeSegments = 0;
  for (size_t index = 0; index < strip.getSegmentsNum(); index++) {
    const Segment &segment = strip.getSegment(index);
    if (segment.isActive() && segment.mode == spriteEffectId) activeSegments++;
  }
  return activeSegments;
}

uint16_t getMaxInstanceCount(uint16_t segmentWidth, uint16_t segmentHeight, uint16_t spriteWidth, uint16_t spriteHeight) {
  if (segmentWidth == 0 || segmentHeight == 0 || spriteWidth == 0 || spriteHeight == 0) return 1;
  if (spriteWidth >= segmentWidth || spriteHeight >= segmentHeight) return 1;
  const uint32_t spriteArea = uint32_t(spriteWidth) * spriteHeight;
  const uint32_t segmentArea = uint32_t(segmentWidth) * segmentHeight;
  const uint16_t fitLimit = min<uint16_t>(SPRITE_MAX_INSTANCES, max<uint32_t>(1, segmentArea / max<uint32_t>(1, spriteArea)));
  return max<uint16_t>(1, fitLimit);
}

uint16_t getDesiredInstanceCount(const Segment &seg, uint16_t segmentWidth, uint16_t segmentHeight, uint16_t spriteWidth, uint16_t spriteHeight) {
  const uint16_t maxInstances = getMaxInstanceCount(segmentWidth, segmentHeight, spriteWidth, spriteHeight);
  if (maxInstances <= 1) return 1;
  return 1 + map(seg.intensity, 0, 255, 0, maxInstances - 1);
}

void generateInstanceLayout(SpriteState &state, uint16_t requestedCount, uint16_t segmentWidth, uint16_t segmentHeight, uint16_t spriteWidth, uint16_t spriteHeight, uint32_t layoutKey) {
  SpriteInstance *instances = getSpriteInstances(&state);
  const int16_t centeredX = (int16_t(segmentWidth) - int16_t(spriteWidth)) / 2;
  const int16_t centeredY = (int16_t(segmentHeight) - int16_t(spriteHeight)) / 2;

  state.layoutKey = layoutKey;
  state.instanceCount = 1;
  instances[0] = { centeredX, centeredY };

  if (requestedCount <= 1 || spriteWidth >= segmentWidth || spriteHeight >= segmentHeight) return;

  const int16_t maxX = max<int16_t>(0, int16_t(segmentWidth) - int16_t(spriteWidth));
  const int16_t maxY = max<int16_t>(0, int16_t(segmentHeight) - int16_t(spriteHeight));
  if (maxX == 0 || maxY == 0) return;

  const uint32_t availableArea = uint32_t(maxX + 1) * uint32_t(maxY + 1);
  const uint16_t spriteSpan = max<uint16_t>(1, max(spriteWidth, spriteHeight));
  uint16_t minDistance = max<uint16_t>(spriteSpan, uint16_t(sqrtf(float(availableArea) / float(requestedCount)) * 0.65f));
  uint32_t minDistanceSq = uint32_t(minDistance) * minDistance;
  const int16_t spriteHalfWidth = spriteWidth / 2;
  const int16_t spriteHalfHeight = spriteHeight / 2;
  SpritePrng prng(layoutKey ^ (layoutKey >> 16));

  uint16_t attempts = 0;
  const uint16_t maxAttempts = requestedCount * 240;
  const uint16_t relaxEvery = max<uint16_t>(16, requestedCount * 12);
  while (state.instanceCount < requestedCount && attempts < maxAttempts) {
    const int16_t candidateX = prng.random16(maxX + 1);
    const int16_t candidateY = prng.random16(maxY + 1);
    const int16_t candidateCenterX = candidateX + spriteHalfWidth;
    const int16_t candidateCenterY = candidateY + spriteHalfHeight;
    bool valid = true;

    for (uint16_t index = 0; index < state.instanceCount; index++) {
      const int32_t dx = candidateCenterX - (instances[index].x + spriteHalfWidth);
      const int32_t dy = candidateCenterY - (instances[index].y + spriteHalfHeight);
      if (uint32_t(dx * dx + dy * dy) < minDistanceSq) {
        valid = false;
        break;
      }
    }

    if (valid) {
      instances[state.instanceCount++] = { candidateX, candidateY };
      continue;
    }

    attempts++;
    if (attempts % relaxEvery == 0 && minDistance > 1) {
      minDistance = max<uint16_t>(1, uint16_t((uint32_t(minDistance) * 7U) / 8U));
      minDistanceSq = uint32_t(minDistance) * minDistance;
    }
  }

  while (state.instanceCount < requestedCount) {
    instances[state.instanceCount++] = { int16_t(prng.random16(maxX + 1)), int16_t(prng.random16(maxY + 1)) };
  }
}

void ensureInstanceLayout(SpriteState &state, const Segment &seg, uint16_t spriteWidth, uint16_t spriteHeight) {
  const uint16_t segmentWidth = seg.vWidth();
  const uint16_t segmentHeight = seg.vHeight();
  const uint16_t desiredCount = getDesiredInstanceCount(seg, segmentWidth, segmentHeight, spriteWidth, spriteHeight);
  uint32_t layoutKey = hashString32(state.assetName);
  layoutKey ^= uint32_t(segmentWidth) << 20;
  layoutKey ^= uint32_t(segmentHeight) << 12;
  layoutKey ^= uint32_t(spriteWidth) << 6;
  layoutKey ^= uint32_t(spriteHeight);
  layoutKey ^= uint32_t(desiredCount) << 26;

  if (state.layoutKey == layoutKey && state.instanceCount == desiredCount) return;
  generateInstanceLayout(state, desiredCount, segmentWidth, segmentHeight, spriteWidth, spriteHeight, layoutKey);
}

bool allocateSpriteState(Segment &seg, const char *assetName, const SpriteFrameSpec *specs, uint16_t frameCount, uint32_t pixelBytes, SpriteState *&state) {
  const size_t metadataBytes = sizeof(SpriteState) + size_t(frameCount) * sizeof(SpriteFrame) + size_t(SPRITE_MAX_INSTANCES) * sizeof(SpriteInstance);
  const size_t dataSize = metadataBytes + pixelBytes;
  seg.deallocateData();
  if (!seg.allocateData(dataSize)) return false;

  state = reinterpret_cast<SpriteState *>(seg.data);
  memset(state, 0, metadataBytes);
  state->magic = SPRITE_STATE_MAGIC;
  strlcpy(state->assetName, assetName, sizeof(state->assetName));
  state->frameCount = frameCount;
  state->pixelDataOffset = metadataBytes;
  state->cacheBytes = pixelBytes;
  state->lastRenderKey = UINT32_MAX;
  state->lastError = SPRITE_ERROR_NONE;

  SpriteFrame *frames = getSpriteFrames(state);
  uint32_t dataOffset = 0;
  for (uint16_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    frames[frameIndex].durationMs = specs[frameIndex].durationMs;
    frames[frameIndex].width = specs[frameIndex].width;
    frames[frameIndex].height = specs[frameIndex].height;
    frames[frameIndex].dataOffset = dataOffset;
    dataOffset += uint32_t(specs[frameIndex].width) * specs[frameIndex].height * sizeof(SpritePixel);
    if (specs[frameIndex].width > state->maxWidth) state->maxWidth = specs[frameIndex].width;
    if (specs[frameIndex].height > state->maxHeight) state->maxHeight = specs[frameIndex].height;
  }

  return true;
}

bool inspectSpriteSpecs(SpriteFrameSpec *specs, uint16_t frameCount, uint16_t &maxWidth, uint16_t &maxHeight, uint32_t &pixelBytes) {
  PNG *decoder = new PNG();
  if (!decoder) return false;

  pixelBytes = 0;
  maxWidth = 0;
  maxHeight = 0;
  bool success = true;
  for (uint16_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    const int openResult = decoder->open(specs[frameIndex].filename, spritePngOpen, spritePngClose, spritePngRead, spritePngSeek, nullptr);
    if (openResult != PNG_SUCCESS) {
      success = false;
      break;
    }

    specs[frameIndex].width = decoder->getWidth();
    specs[frameIndex].height = decoder->getHeight();
    decoder->close();
    if (specs[frameIndex].width == 0 || specs[frameIndex].height == 0 || specs[frameIndex].width > SPRITE_MAX_DIMENSION || specs[frameIndex].height > SPRITE_MAX_DIMENSION) {
      success = false;
      break;
    }

    if (specs[frameIndex].width > maxWidth) maxWidth = specs[frameIndex].width;
    if (specs[frameIndex].height > maxHeight) maxHeight = specs[frameIndex].height;
    pixelBytes += uint32_t(specs[frameIndex].width) * specs[frameIndex].height * sizeof(SpritePixel);
    if (pixelBytes > SPRITE_CACHE_BUDGET) {
      success = false;
      break;
    }
  }

  delete decoder;
  return success;
}

bool decodeSpriteCache(SpriteState &state, const SpriteFrameSpec *specs) {
  PNG *decoder = new PNG();
  if (!decoder) return false;

  bool success = true;
  SpriteFrame *frames = getSpriteFrames(&state);
  for (uint16_t frameIndex = 0; frameIndex < state.frameCount; frameIndex++) {
    const int openResult = decoder->open(specs[frameIndex].filename, spritePngOpen, spritePngClose, spritePngRead, spritePngSeek, spriteCacheWrite);
    if (openResult != PNG_SUCCESS) {
      success = false;
      break;
    }

    SpriteCacheLoadContext context;
    context.pixels = reinterpret_cast<SpritePixel *>(getSpritePixelBlob(&state) + frames[frameIndex].dataOffset);
    context.width = frames[frameIndex].width;
    context.height = frames[frameIndex].height;

    const int decodeResult = decoder->decode(&context, PNG_FAST_PALETTE);
    decoder->close();
    if (decodeResult != PNG_SUCCESS) {
      success = false;
      break;
    }
  }

  delete decoder;
  return success;
}

bool loadSpriteFromSpecs(Segment &seg, const char *assetName, SpriteFrameSpec *specs, uint16_t frameCount, SpriteState *&state, byte &errorCode) {
  const uint32_t start = millis();
  uint16_t maxWidth = 0;
  uint16_t maxHeight = 0;
  uint32_t pixelBytes = 0;
  if (!inspectSpriteSpecs(specs, frameCount, maxWidth, maxHeight, pixelBytes)) {
    errorCode = (pixelBytes > SPRITE_CACHE_BUDGET) ? SPRITE_ERROR_TOO_LARGE : SPRITE_ERROR_PNG;
    return false;
  }

  if (!allocateSpriteState(seg, assetName, specs, frameCount, pixelBytes, state)) {
    errorCode = SPRITE_ERROR_ALLOC;
    return false;
  }

  if (!decodeSpriteCache(*state, specs)) {
    seg.deallocateData();
    state = nullptr;
    errorCode = SPRITE_ERROR_PNG;
    return false;
  }

  noteSpriteLoad(maxWidth, maxHeight, frameCount, pixelBytes, millis() - start);
  errorCode = SPRITE_ERROR_NONE;
  return true;
}

byte loadSingleSprite(Segment &seg, const char *assetName, SpriteState *&state) {
  auto *specs = static_cast<SpriteFrameSpec *>(calloc(1, sizeof(SpriteFrameSpec)));
  if (!specs) return SPRITE_ERROR_ALLOC;
  specs[0].durationMs = 0;
  strlcpy(specs[0].filename, assetName, sizeof(specs[0].filename));

  byte errorCode = SPRITE_ERROR_NONE;
  const bool success = loadSpriteFromSpecs(seg, assetName, specs, 1, state, errorCode);
  free(specs);
  return success ? SPRITE_ERROR_NONE : errorCode;
}

byte loadAnimDef(Segment &seg, const char *assetName, SpriteState *&state) {
  File file = WLED_FS.open(assetName, "r");
  if (!file) return SPRITE_ERROR_FILE_MISSING;

  const size_t capacity = min<size_t>(JSON_BUFFER_SIZE, max<size_t>(512, file.size() * 4));
  PSRAMDynamicJsonDocument doc(capacity);
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error || !doc.is<JsonArray>()) return SPRITE_ERROR_BAD_ANIMDEF;

  JsonArray framesJson = doc.as<JsonArray>();
  if (framesJson.isNull() || framesJson.size() == 0) return SPRITE_ERROR_BAD_ANIMDEF;

  auto *specs = static_cast<SpriteFrameSpec *>(calloc(framesJson.size(), sizeof(SpriteFrameSpec)));
  if (!specs) return SPRITE_ERROR_ALLOC;

  uint16_t frameIndex = 0;
  byte errorCode = SPRITE_ERROR_BAD_ANIMDEF;
  for (JsonObject frameJson : framesJson) {
    const char *filename = frameJson["filename"] | "";
    if (filename[0] == '\0') {
      free(specs);
      return SPRITE_ERROR_BAD_ANIMDEF;
    }
    resolveFramePath(assetName, filename, specs[frameIndex].filename);
    if (specs[frameIndex].filename[0] == '\0') {
      free(specs);
      return SPRITE_ERROR_BAD_ANIMDEF;
    }
    specs[frameIndex].durationMs = max<uint16_t>(1, frameJson["duration"] | 100);
    frameIndex++;
  }

  const bool success = loadSpriteFromSpecs(seg, assetName, specs, frameIndex, state, errorCode);
  free(specs);
  return success ? SPRITE_ERROR_NONE : errorCode;
}

byte ensureSpriteState(Segment &seg, SpriteState *&state) {
  if (!seg.name) return SPRITE_ERROR_NO_NAME;

  char assetName[WLED_MAX_SEGNAME_LEN + 2];
  normalizeRootPath(seg.name, assetName);
  if (assetName[0] == '\0') return SPRITE_ERROR_NO_NAME;

  state = getSpriteState(seg);
  if (state && strncmp(state->assetName, assetName, sizeof(state->assetName)) == 0) return SPRITE_ERROR_NONE;

  byte errorCode = SPRITE_ERROR_UNSUPPORTED;
  if (endsWithIgnoreCase(assetName, ".png")) {
    errorCode = loadSingleSprite(seg, assetName, state);
  } else if (endsWithIgnoreCase(assetName, ".json")) {
    errorCode = loadAnimDef(seg, assetName, state);
  }

  if (state) state->lastError = errorCode;
  noteSpriteError(errorCode);
  return errorCode;
}

uint16_t currentFrameDuration(const SpriteState *state) {
  if (!state || state->frameCount == 0) return 0;
  const SpriteFrame *frames = getSpriteFrames(state);
  return frames[state->currentFrame].durationMs;
}

inline uint32_t rotl32(uint32_t value, uint8_t shift) {
  return (value << shift) | (value >> (32 - shift));
}

uint32_t buildRenderKey(const Segment &seg, const SpriteState &state, uint16_t motionOffset) {
  uint32_t key = state.layoutKey;
  key ^= uint32_t(state.currentFrame) << 24;
  key ^= uint32_t(motionOffset) << 8;
  key ^= uint32_t(seg.check1) << 2;
  key ^= uint32_t(seg.check2) << 3;
  key ^= uint32_t(seg.check3) << 4;
  key ^= uint32_t(seg.palette) << 16;
  key ^= rotl32(seg.colors[0], 5);
  key ^= rotl32(seg.colors[1], 13);
  key ^= rotl32(seg.colors[2], 21);
  return key;
}

void updateAnimationFrame(SpriteState *state) {
  if (!state || state->frameCount <= 1) return;

  if (state->nextFrameTime == 0) {
    state->nextFrameTime = strip.now + currentFrameDuration(state);
    return;
  }

  while ((int32_t)(strip.now - state->nextFrameTime) >= 0) {
    state->currentFrame = (state->currentFrame + 1) % state->frameCount;
    state->nextFrameTime += currentFrameDuration(state);
  }
}

uint32_t getMaskColor(const SpriteRenderContext &context, int16_t destX, int16_t destY) {
  if (!context.useGradient) return SEGCOLOR(0);

  uint8_t paletteIndex = 0;
  if (context.useHorizontalGradient) {
    if (context.segmentWidth <= 1) return SEGCOLOR(0);
    paletteIndex = map(destX, 0, context.segmentWidth - 1, 0, 255);
  } else {
    if (context.segmentHeight <= 1) return SEGCOLOR(0);
    paletteIndex = map(destY, 0, context.segmentHeight - 1, 0, 255);
  }

  return context.segment->color_from_palette(paletteIndex, false, false, 0);
}

void blendSpritePixel(const SpriteRenderContext &context, int16_t destX, int16_t destY, uint32_t sourceColor, uint8_t alpha) {
  if (alpha == 0) return;
  if ((unsigned)destX >= context.segmentWidth || (unsigned)destY >= context.segmentHeight) return;

  const uint32_t outputColor = context.useMask ? getMaskColor(context, destX, destY) : sourceColor;
  if (context.segment->blendMode == 17) {
    context.segment->setPixelColorXY(destX, destY, RGBW32(R(outputColor), G(outputColor), B(outputColor), alpha));
    return;
  }

  context.segment->blendPixelColorXY(destX, destY, outputColor, alpha);
}

int spriteCacheWrite(PNGDRAW *pDraw) {
  auto *context = static_cast<SpriteCacheLoadContext *>(pDraw->pUser);
  if (!context) return 0;
  if (pDraw->y < 0 || pDraw->y >= context->height) return 0;
  if (pDraw->iWidth <= 0 || pDraw->iWidth > context->width) return 0;

  SpritePixel *row = context->pixels + size_t(pDraw->y) * context->width;
  for (uint16_t pixelX = 0; pixelX < context->width; pixelX++) {
    row[pixelX].color565 = 0;
    row[pixelX].alpha = 0;
  }

  for (int pixelX = 0; pixelX < pDraw->iWidth; pixelX++) {
    uint32_t color = 0;
    uint8_t alpha = 0;
    if (!decodeSpritePixel(pDraw, pixelX, color, alpha) || alpha == 0) continue;
    row[pixelX].color565 = rgb32To565(color);
    row[pixelX].alpha = alpha;
  }

  return 1;
}

void *spritePngOpen(const char *filename, int32_t *size) {
  spritePngFile = WLED_FS.open(filename, FILE_READ);
  if (!spritePngFile) return nullptr;
  if (size) *size = spritePngFile.size();
  return &spritePngFile;
}

void spritePngClose(void *handle) {
  auto *pngFile = static_cast<File *>(handle);
  if (pngFile) pngFile->close();
}

int32_t spritePngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  auto *pngFile = static_cast<File *>(handle->fHandle);
  return pngFile ? int32_t(pngFile->read(buffer, length)) : 0;
}

int32_t spritePngSeek(PNGFILE *handle, int32_t position) {
  auto *pngFile = static_cast<File *>(handle->fHandle);
  return (pngFile && pngFile->seek(position)) ? 1 : 0;
}

uint16_t computeMotionOffset(uint8_t speed, uint16_t segmentWidth, uint16_t spriteWidth) {
  if (speed == 0 || segmentWidth == 0 || spriteWidth > segmentWidth) return 0;
  // sx=0 freezes, sx=1 should already feel like a *very* gentle drift — 1 px/sec
  // gives ~32 seconds to cross a 32-wide panel, which is roughly right for
  // ~2 m/s wind. Top end stays at 128 px/sec for storm-level wind.
  const uint16_t pixelsPerSecond = map(speed, 1, 255, 1, 128);
  return ((strip.now * pixelsPerSecond) / 1000U) % segmentWidth;
}

byte drawSpriteFrame(Segment &seg, SpriteState &state, const SpriteFrame &frame) {
  ensureInstanceLayout(state, seg, state.maxWidth, state.maxHeight);
  const uint16_t motionOffset = computeMotionOffset(seg.speed, seg.vWidth(), state.maxWidth);

  SpriteRenderContext context;
  context.segment = &seg;
  context.segmentWidth = uint16_t(seg.vWidth());
  context.segmentHeight = uint16_t(seg.vHeight());
  context.spriteWidth = frame.width;
  context.spriteHeight = frame.height;
  context.motionOffset = motionOffset;
  context.instanceCount = state.instanceCount;
  context.useMask = seg.check1;
  context.useGradient = seg.check1 && seg.check2;
  context.useHorizontalGradient = seg.check3;
  context.moving = seg.speed > 0 && state.maxWidth <= seg.vWidth();
  context.instances = getSpriteInstances(&state);

  const uint32_t renderKey = buildRenderKey(seg, state, motionOffset);
  if (renderKey == state.lastRenderKey) {
    noteSpriteDraw(0, 0, true);
    return SPRITE_ERROR_NONE;
  }

  const uint32_t drawStart = micros();
  uint32_t blitPixels = 0;
  if (context.segment->blendMode == 17) context.segment->fill(0);
  // Preserve the already-rendered segment contents so PNG alpha blends over
  // the current segment buffer. Alpha-over mode clears above and stores PNG
  // alpha in W for WLED's final segment compositor.
  const auto *pixels = reinterpret_cast<const SpritePixel *>(getSpritePixelBlob(&state) + frame.dataOffset);
  for (uint16_t y = 0; y < frame.height; y++) {
    const SpritePixel *row = pixels + size_t(y) * frame.width;
    for (uint16_t x = 0; x < frame.width; x++) {
      if (row[x].alpha == 0) continue;
      const uint32_t color = context.useMask ? SEGCOLOR(0) : rgb565ToRgb32(row[x].color565);
      for (uint16_t instanceIndex = 0; instanceIndex < context.instanceCount; instanceIndex++) {
        int16_t drawX = context.instances[instanceIndex].x;
        const int16_t drawY = context.instances[instanceIndex].y;
        if (context.moving) {
          int32_t movedX = drawX + context.motionOffset;
          movedX %= int32_t(context.segmentWidth);
          if (movedX < 0) movedX += context.segmentWidth;
          drawX = movedX;
        }

        const int16_t destX = drawX + x;
        const int16_t destY = drawY + y;
        blendSpritePixel(context, destX, destY, color, row[x].alpha);
        blitPixels++;

        if (context.moving && context.spriteWidth <= context.segmentWidth && destX >= context.segmentWidth) {
          blendSpritePixel(context, destX - context.segmentWidth, destY, color, row[x].alpha);
          blitPixels++;
        }
      }
    }
  }

  state.lastRenderKey = renderKey;
  noteSpriteDraw(micros() - drawStart, blitPixels, false);
  return SPRITE_ERROR_NONE;
}

uint16_t precipitationRandom(PrecipitationState &state, uint16_t upperExclusive) {
  if (upperExclusive <= 1) return 0;
  state.prngState ^= state.prngState << 13;
  state.prngState ^= state.prngState >> 17;
  state.prngState ^= state.prngState << 5;
  return uint16_t(state.prngState % upperExclusive);
}

uint16_t getPrecipitationParticleCapacity(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0) return 0;
  const uint32_t pixels = uint32_t(width) * height;
  const uint16_t areaLimit = max<uint16_t>(8, min<uint32_t>(PRECIPITATION_MAX_PARTICLES, pixels / 3U + 6U));
  const size_t memoryLimit = (MAX_SEGMENT_DATA > sizeof(PrecipitationState))
    ? (MAX_SEGMENT_DATA - sizeof(PrecipitationState)) / sizeof(PrecipitationParticle)
    : 0;
  return min<uint16_t>(areaLimit, memoryLimit);
}

uint16_t getPrecipitationActiveTarget(const Segment &seg, uint16_t capacity) {
  if (capacity == 0) return 0;
  if (seg.intensity == 0) return 0;
  return map(seg.intensity, 1, 255, 1, capacity);
}

bool ensurePrecipitationState(Segment &seg, PrecipitationState *&state) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  const uint16_t capacity = getPrecipitationParticleCapacity(width, height);
  const size_t dataSize = sizeof(PrecipitationState) + size_t(capacity) * sizeof(PrecipitationParticle);

  state = nullptr;
  if (capacity == 0) return false;

  if (seg.data) {
    auto *existing = reinterpret_cast<PrecipitationState *>(seg.data);
    if (existing->magic == PRECIPITATION_STATE_MAGIC && existing->width == width && existing->height == height && existing->particleCount == capacity) {
      state = existing;
      return true;
    }
  }

  seg.deallocateData();
  if (!seg.allocateData(dataSize)) return false;

  state = reinterpret_cast<PrecipitationState *>(seg.data);
  memset(state, 0, dataSize);
  state->magic = PRECIPITATION_STATE_MAGIC;
  state->lastUpdate = strip.now;
  state->prngState = 0x9E3779B9UL ^ (uint32_t(width) << 16) ^ uint32_t(height) ^ strip.now;
  state->width = width;
  state->height = height;
  state->particleCount = capacity;
  return true;
}

void respawnPrecipitationParticle(PrecipitationState &state, PrecipitationParticle &particle, const Segment &seg, bool startAbove) {
  const uint16_t width = max<uint16_t>(1, state.width);

  particle.state = 1;
  particle.age = 0;
  particle.ttl = 0;
  particle.seed = precipitationRandom(state, 255);
  particle.x = int32_t(precipitationRandom(state, width) * PRECIPITATION_FP_SCALE + precipitationRandom(state, PRECIPITATION_FP_SCALE));
  particle.y = startAbove
    ? -int32_t(precipitationRandom(state, max<uint16_t>(1, state.height)) * PRECIPITATION_FP_SCALE)
    : int32_t(precipitationRandom(state, max<uint16_t>(1, state.height)) * PRECIPITATION_FP_SCALE);
  particle.vx = 0;
  particle.vy = 0;
}

int16_t precipitationBaseFallSpeed(const Segment &seg) {
  // Slider range tuned for matrix scenes: sx=1 ≈ 4 px/sec (gentle drizzle),
  // sx=128 ≈ 34 px/sec (normal rain), sx=255 ≈ 64 px/sec (downpour).
  return seg.speed == 0 ? 0 : int16_t(map(seg.speed, 1, 255, 4, 64));
}

uint8_t precipitationSplashAgeStep(const Segment &seg, uint16_t deltaMs) {
  const int16_t baseFall = precipitationBaseFallSpeed(seg);
  if (baseFall <= 0 || deltaMs == 0) return 0;

  const uint32_t numerator = uint32_t(deltaMs) * uint32_t(baseFall) * PRECIPITATION_SPLASH_TIME_SCALE;
  const uint32_t denominator = uint32_t(PRECIPITATION_SPLASH_FRAME_MS) * PRECIPITATION_REFERENCE_FALL;
  const uint32_t step = max<uint32_t>(1U, (numerator + denominator / 2U) / denominator);
  return uint8_t(min<uint32_t>(255U, step));
}

void updatePrecipitationMotion(PrecipitationParticle &particle, const Segment &seg) {
  const int16_t baseFall = precipitationBaseFallSpeed(seg);
  // Parallax: each particle's seed acts as a depth value (0..255 ≈ background
  // ..foreground). Background drops fall slower (~55%) and render dimmer; full
  // speed/brightness at depth=255. This is what gives the rain a layered feel
  // instead of a flat wall of uniform particles.
  const int16_t depthMul = int16_t(140 + (uint16_t(particle.seed) >> 1));  // 140..267
  const int16_t fallSpeed = int16_t((int32_t(baseFall) * depthMul) >> 8);
  const int16_t shear = int16_t(seg.custom2) - 128;
  particle.vx = int16_t((int32_t)shear * max<int16_t>(8, fallSpeed) / 128);
  particle.vy = fallSpeed;
}

// Parallax-modulated despawn row. Background drops (low seed = far away)
// land near the horizon (upper part of the seg); foreground drops land at
// the bottom edge. Splash impact uses the same Y, so the depth cue carries
// through the whole drop lifecycle.
uint16_t precipitationDespawnY(uint8_t depth, uint16_t height) {
  if (height < 2) return height ? height - 1 : 0;
  const uint16_t topY = (uint32_t(height) * 5U) / 8U; // ~62.5% from the top
  const uint16_t spanY = (height - 1) - topY;
  return topY + uint16_t((uint32_t(depth) * spanY) >> 8);
}

void startPrecipitationSplash(PrecipitationState &state, PrecipitationParticle &particle, const Segment &seg) {
  const uint8_t bounciness = seg.custom1;
  if (bounciness == 0) {
    respawnPrecipitationParticle(state, particle, seg, true);
    return;
  }

  particle.state = 2;
  particle.age = 0;
  // 6..21 normal-speed frames at ~30fps = 200..700ms. Splash age is stored in
  // fixed-point frame units, then advanced by the same sx time scale as falling.
  particle.ttl = uint8_t((6 + (bounciness >> 4)) * PRECIPITATION_SPLASH_TIME_SCALE);
  // Snap to the parallax-correct row so the burst origin sits on an integer pixel.
  particle.y = int32_t(precipitationDespawnY(particle.seed, state.height)) * PRECIPITATION_FP_SCALE;
}

void drawPrecipitationPixel(Segment &seg, int16_t x, int16_t y, uint32_t color, uint8_t alpha) {
  if (alpha == 0 || (unsigned)x >= seg.vWidth() || (unsigned)y >= seg.vHeight()) return;
  // bm=17 (Alpha Over): write the source color with W=alpha and let WLED's
  // segment compositor do the over-blend cleanly. Other blend modes go through
  // the standard add-style blend.
  if (seg.blendMode == 17) {
    seg.setPixelColorXY(x, y, RGBW32(R(color), G(color), B(color), alpha));
    return;
  }
  seg.blendPixelColorXY(x, y, color, alpha);
}

// Parallax depth → output alpha. Background drops (low depth) ≈ 30% bright,
// foreground at full 100%. Keeps the rain from looking like a flat curtain.
inline uint8_t precipitationDepthAlpha(uint8_t depth) {
  return uint8_t(80 + ((uint16_t(depth) * 175U) >> 8));  // 80..254
}

// Trails fade from SEGCOLOR(1) toward SEGCOLOR(2) along their length. With
// alpha-over compositing (bm=17), a "transparent" tail is best achieved by
// fading alpha, not by writing black RGB (which darkens the background). So
// when SEGCOLOR(2) is solid black we treat it as "fade alpha only" and keep
// the RGB at SEGCOLOR(1); otherwise the user is explicitly asking for a
// color-blend tail and we honor it.
void drawPrecipitationTrail(Segment &seg, const PrecipitationParticle &particle,
                            int16_t headX, int16_t headY, uint8_t trailLength,
                            uint8_t depthAlpha, uint8_t fadeMul) {
  if (trailLength == 0 || particle.vy == 0) return;
  const int16_t denom = max<int16_t>(1, abs(particle.vy));
  const uint32_t col1 = SEGCOLOR(1);
  const uint32_t col2 = SEGCOLOR(2);
  const bool col2IsBlack = (R(col2) | G(col2) | B(col2)) == 0;
  for (uint8_t index = trailLength; index > 0; index--) {
    const int16_t trailX = headX - int16_t((int32_t(particle.vx) * index) / denom);
    const int16_t trailY = headY - index;
    const uint8_t blend = (uint16_t(index) * 255U) / trailLength;
    const uint32_t trailColor = col2IsBlack ? col1 : color_blend(col1, col2, blend);
    // Three alpha multipliers stacked: head-to-tail (255-blend), splash decay
    // (fadeMul = 255 while falling, ramps to 0 over the splash window), and
    // parallax depth.
    const uint32_t a = (uint32_t(255 - blend) * fadeMul * depthAlpha) / (255U * 255U);
    drawPrecipitationPixel(seg, trailX, trailY, trailColor, uint8_t(a));
  }
}

void drawPrecipitationParticle(Segment &seg, const PrecipitationParticle &particle) {
  const int16_t headX = particle.x >> PRECIPITATION_FP_SHIFT;
  const int16_t headY = particle.y >> PRECIPITATION_FP_SHIFT;
  const uint8_t depthAlpha = precipitationDepthAlpha(particle.seed);
  const uint8_t trailLength = seg.custom3;

  if (particle.state == 2) {
    // 2D splash. Three deterministic gravity-arc droplets per impact, with a
    // few coherence rules:
    //   - Splash gravity scales with the particle's fall speed: heavy rain
    //     produces snappy splashes, drizzle splashes hang in the air.
    //   - c1 (Bounciness) sets the initial upward velocity. c1=255 still caps
    //     peak height around 5 pixels — at higher bounciness the visual reads
    //     as "hail".
    //   - Per-droplet seed jitter so droplets don't all jump in lockstep.
    //   - Horizontal spread fixed (no slider) — the old c1=Splash radius slot
    //     is now bounciness, since "splash radius" wasn't pulling its weight.
    const uint8_t ttl = particle.ttl ? particle.ttl : PRECIPITATION_SPLASH_TIME_SCALE;
    const uint8_t age = min<uint8_t>(particle.age, ttl);
    const uint8_t progress = (uint16_t(age) * 255U) / ttl;
    const uint8_t fade = 255 - progress;

    const uint8_t bounciness = seg.custom1;
    // Both the initial upward velocity (vy0) and gravity scale with the
    // drop's actual fall speed (which itself carries parallax — foreground
    // drops fall faster than background ones, so they also splash harder).
    // Bounciness adds a base kick; sx-driven speedKick adds proportional
    // energy. Cap vy0 at 5 so the upward arc stays within the eye-line cap,
    // and let gravity scale up too so peak height stays bounded.
    const int16_t baseVy0   = max<int16_t>(1, int16_t(bounciness) / 64);   // 1..3 (4 at 255)
    const int16_t speedKick = max<int16_t>(0, (abs(particle.vy) - 16) / 16); // 0..3 (4 at vy≥80)
    const int16_t vy0Max    = min<int16_t>(5, baseVy0 + speedKick);
    // Splash gravity tied to fall speed for coherence. Default vy≈34 → g=2;
    // downpour vy=64 → g=4 (snappy); drizzle vy=4 → g=1 (hangs).
    const int16_t splashG   = max<int16_t>(1, abs(particle.vy) / 16);

    static const int8_t SPLASH_VX[3] = { -1, 0, 1 };

    for (uint8_t i = 0; i < 3; i++) {
      const int16_t t = int16_t(age) + PRECIPITATION_SPLASH_TIME_SCALE;
      // Per-droplet vy0 with seed-driven variance. The middle droplet (i=1)
      // gets the +1 boost so it visibly peaks higher than the outer two.
      const uint8_t mix = uint8_t((particle.seed * 17U + i * 53U) & 0xFFU);
      const int16_t vy0 = -(vy0Max + (i == 1 ? 1 : 0) - int16_t(mix & 0x01));
      const int16_t dx = int16_t((int32_t(SPLASH_VX[i]) * t) / PRECIPITATION_SPLASH_TIME_SCALE);
      const int32_t dyFixed = int32_t(vy0) * t
        + (int32_t(splashG) * t * (t - PRECIPITATION_SPLASH_TIME_SCALE))
          / (2 * PRECIPITATION_SPLASH_TIME_SCALE);
      const int16_t dy = int16_t(dyFixed / PRECIPITATION_SPLASH_TIME_SCALE);
      const int16_t px = headX + dx;
      const int16_t py = headY + dy;
      const uint8_t alpha = uint8_t((uint16_t(fade) * depthAlpha) / 255);
      drawPrecipitationPixel(seg, px, py, SEGCOLOR(0), alpha);
    }

    // Keep the trail alive during the splash window, fading with splash age,
    // so the drop doesn't visually pop out of existence at the moment of impact.
    drawPrecipitationTrail(seg, particle, headX, headY, trailLength, depthAlpha, fade);
    return;
  }

  drawPrecipitationTrail(seg, particle, headX, headY, trailLength, depthAlpha, 255);
  drawPrecipitationPixel(seg, headX, headY, SEGCOLOR(0), depthAlpha);
}

void updatePrecipitationParticle(PrecipitationState &state, PrecipitationParticle &particle, const Segment &seg, uint16_t index, uint16_t activeTarget, uint16_t deltaMs) {
  if (index >= activeTarget) {
    particle.state = 0;
    return;
  }

  if (particle.state == 0) {
    respawnPrecipitationParticle(state, particle, seg, false);
    return;
  }

  if (particle.state == 2) {
    const uint8_t ageStep = precipitationSplashAgeStep(seg, deltaMs);
    if (ageStep == 0) return;
    particle.age = (particle.age > 255U - ageStep) ? 255 : particle.age + ageStep;
    if (particle.age >= particle.ttl) respawnPrecipitationParticle(state, particle, seg, true);
    return;
  }

  updatePrecipitationMotion(particle, seg);
  if (seg.speed == 0) return;

  const int32_t moveX = (int32_t(particle.vx) * deltaMs * PRECIPITATION_FP_SCALE) / 1000L;
  const int32_t moveY = (int32_t(particle.vy) * deltaMs * PRECIPITATION_FP_SCALE) / 1000L;
  particle.x += moveX;
  particle.y += moveY;

  const int32_t maxX = int32_t(state.width) * PRECIPITATION_FP_SCALE;
  while (particle.x < 0) particle.x += maxX;
  while (particle.x >= maxX) particle.x -= maxX;

  const uint16_t despawnY = precipitationDespawnY(particle.seed, state.height);
  if (particle.y >= int32_t(despawnY) * PRECIPITATION_FP_SCALE) startPrecipitationSplash(state, particle, seg);
}

void renderPrecipitationToSegment(Segment &seg) {
  PrecipitationState *state = nullptr;
  if (!ensurePrecipitationState(seg, state) || !state) {
    seg.fill(BLACK);
    return;
  }

  uint16_t deltaMs = min<uint32_t>(100, strip.now - state->lastUpdate);
  state->lastUpdate = strip.now;
  state->activeTarget = getPrecipitationActiveTarget(seg, state->particleCount);

  seg.fill(BLACK);
  PrecipitationParticle *particles = getPrecipitationParticles(state);
  for (uint16_t index = 0; index < state->particleCount; index++) {
    updatePrecipitationParticle(*state, particles[index], seg, index, state->activeTarget, deltaMs);
    if (particles[index].state != 0) drawPrecipitationParticle(seg, particles[index]);
  }
}

uint8_t ditheredGradientThreshold(uint16_t x, uint16_t y) {
  static const uint8_t bayer4x4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
  };
  return bayer4x4[((y & 0x03) << 2) | (x & 0x03)];
}

uint8_t getDitheredGradientPaletteIndex(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t dither, uint8_t rotation) {
  if (width <= 1 && height <= 1) return 0;

  const float angle = (float(rotation) / 255.0f) * DITHERED_GRADIENT_TWO_PI;
  const float axisX = sinf(angle);
  const float axisY = cosf(angle);
  const float maxX = (width > 1) ? float(width - 1) : 0.0f;
  const float maxY = (height > 1) ? float(height - 1) : 0.0f;
  const float p0 = 0.0f;
  const float p1 = axisX * maxX;
  const float p2 = axisY * maxY;
  const float p3 = p1 + p2;
  const float minProjection = min(min(p0, p1), min(p2, p3));
  const float maxProjection = max(max(p0, p1), max(p2, p3));
  const float range = maxProjection - minProjection;
  if (range <= 0.001f) return 0;

  float paletteIndex = ((axisX * x + axisY * y - minProjection) * 255.0f) / range;
  if (dither > 0) {
    const float ordered = (float(ditheredGradientThreshold(x, y)) - 7.5f) / 15.0f;
    paletteIndex += ordered * ((float(dither) * 96.0f) / 255.0f);
  }

  return uint8_t(constrain(int16_t(lroundf(paletteIndex)), 0, 255));
}

void renderDitheredGradientToSegment(Segment &seg) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  if (width == 0 || height == 0) return;

  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      const uint8_t paletteIndex = getDitheredGradientPaletteIndex(x, y, width, height, seg.speed, seg.intensity);
      seg.setPixelColorXY(x, y, seg.color_from_palette(paletteIndex, false, false, 0));
    }
  }
}

// Mixes (starIndex, variant, panel size) into a 32-bit hash. Stable across
// calls so a given segment always lays its stars in the same spots; varies
// with width/height so different panels produce different layouts.
uint32_t starfieldHash(uint16_t starIndex, uint8_t variant, uint16_t width, uint16_t height) {
  uint32_t h = 0x9E3779B9UL;
  h ^= uint32_t(starIndex) << 1;
  h *= 0x85EBCA6BUL;
  h ^= uint32_t(variant) << 8;
  h *= 0xC2B2AE35UL;
  h ^= uint32_t(width) << 5;
  h *= 0x27D4EB2FUL;
  h ^= uint32_t(height);
  h ^= h >> 16;
  h *= 0x85EBCA6BUL;
  h ^= h >> 13;
  return h;
}

void renderStarfieldGradient(Segment &seg, uint16_t width, uint16_t height) {
  // Vertical palette gradient — escape hatch for using the starfield seg as a
  // plain sky backdrop without bringing in a second effect.
  for (uint16_t y = 0; y < height; y++) {
    const uint8_t paletteIndex = (height <= 1) ? 0 : uint8_t((uint32_t(y) * 255U) / (height - 1U));
    const uint32_t color = seg.color_from_palette(paletteIndex, false, false, 0);
    for (uint16_t x = 0; x < width; x++) {
      seg.setPixelColorXY(x, y, color);
    }
  }
}

void renderStarfieldToSegment(Segment &seg) {
  const uint16_t width = uint16_t(seg.vWidth());
  const uint16_t height = uint16_t(seg.vHeight());
  if (width == 0 || height == 0) return;

  // check1 = "Gradient" override — paints a static palette gradient instead of stars.
  if (seg.check1) {
    renderStarfieldGradient(seg, width, height);
    return;
  }

  seg.fill(BLACK);
  if (seg.intensity == 0) return;

  // Density: ix maps to 1..(pixels/16), matching the raster reference's ~6% coverage
  // at full slider. At default ix=160 a 32x10 segment renders ~13 stars.
  const uint32_t pixels = uint32_t(width) * height;
  const uint16_t maxStars = max<uint16_t>(1, uint16_t(pixels / 16U));
  const uint16_t starCount = uint16_t(1U + map(seg.intensity, 1, 255, 0, max<uint16_t>(0, maxStars - 1U)));

  // Twinkle rate: sx=128 reproduces the legacy raster's 1.6 rad/s pulse, which
  // is the "spot on" feel the renderer originally landed on.
  const float rate = (float(seg.speed) / 128.0f) * STARFIELD_DEFAULT_RATE;
  const float timeS = float(strip.now) / 1000.0f;

  const uint32_t baseColor = SEGCOLOR(0);

  for (uint16_t i = 0; i < starCount; i++) {
    const uint32_t hx = starfieldHash(i, 0, width, height);
    const uint32_t hy = starfieldHash(i, 1, width, height);
    const uint32_t hs = starfieldHash(i, 2, width, height);
    const uint16_t x = uint16_t(hx % width);
    const uint16_t y = uint16_t(hy % height);
    const float seed = float(hs & 0x3FF) / 1024.0f;

    // sin(time * rate + seed * 9) * 0.5 + 0.5 → 0..1, then re-mapped to 0.18..1.0
    // alpha so even the dimmest twinkle stays faintly visible (matches raster).
    const float twinkle = sinf(timeS * rate + seed * 9.0f) * 0.5f + 0.5f;
    const float fade = STARFIELD_MIN_FADE + twinkle * STARFIELD_FADE_SPAN;
    const uint8_t alpha = uint8_t(constrain(int16_t(fade * 255.0f + 0.5f), 0, 255));

    if (seg.blendMode == 17) {
      seg.setPixelColorXY(x, y, RGBW32(R(baseColor), G(baseColor), B(baseColor), alpha));
    } else {
      seg.blendPixelColorXY(x, y, baseColor, alpha);
    }
  }
}

void resetSpriteRuntime(Segment &seg) {
  auto *state = getSpriteState(seg);
  if (!state) return;
  state->lastRenderKey = UINT32_MAX;
}

byte renderUsermodSpriteToSegment(Segment &seg) {
  if (!seg.name) return SPRITE_ERROR_NO_NAME;
  if (!strip.isMatrix || !seg.is2D()) return SPRITE_ERROR_UNSUPPORTED;

  SpriteState *state = nullptr;
  const byte stateResult = ensureSpriteState(seg, state);
  if (stateResult != SPRITE_ERROR_NONE) return stateResult;
  if (!state || state->frameCount == 0) return SPRITE_ERROR_BAD_ANIMDEF;

  updateAnimationFrame(state);
  const SpriteFrame *frames = getSpriteFrames(state);
  return drawSpriteFrame(seg, *state, frames[state->currentFrame]);
}

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
