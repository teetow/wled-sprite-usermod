// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.


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

