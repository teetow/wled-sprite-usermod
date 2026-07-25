// Included from wled_sprite_usermod.cpp inside namespace sprite_usermod.

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

void noteSpriteCacheClone() {
  spriteStats.cacheClones++;
  spriteStats.lastError = SPRITE_ERROR_NONE;
}

void noteSpriteSharedCacheHit() {
  spriteStats.sharedCacheHits++;
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

uint32_t spriteRuntimeSeed(const Segment &seg, const char *assetName) {
  uint32_t seed = hashString32(assetName);
  seed ^= uint32_t(seg.start) << 20;
  seed ^= uint32_t(seg.stop) << 12;
  seed ^= uint32_t(seg.startY) << 6;
  seed ^= uint32_t(seg.stopY);
  seed ^= strip.now;
  seed ^= micros();
  return seed ? seed : 0x9E3779B9UL;
}

void resetSpritePlaybackRuntime(SpriteState &state, const Segment &seg) {
  state.layoutKey = 0;
  state.lastRenderKey = UINT32_MAX;
  state.nextFrameTime = 0;
  state.prngState = spriteRuntimeSeed(seg, state.assetName);
}

uint32_t spriteStateRandomValue(SpriteState &state) {
  state.prngState ^= state.prngState << 13;
  state.prngState ^= state.prngState >> 17;
  state.prngState ^= state.prngState << 5;
  if (state.prngState == 0) state.prngState = 0x9E3779B9UL;
  return state.prngState;
}

uint32_t spriteStateRandom(SpriteState &state, uint32_t upperExclusive) {
  if (upperExclusive <= 1) return 0;
  return spriteStateRandomValue(state) % upperExclusive;
}

int16_t floorSpriteFixed(int32_t value) {
  if (value >= 0) return int16_t(value >> SPRITE_SUBPIXEL_SHIFT);
  return -int16_t(((-value) + SPRITE_SUBPIXEL_SCALE - 1) >> SPRITE_SUBPIXEL_SHIFT);
}

int16_t spriteInstancePixelX(const SpriteInstance &instance) {
  return floorSpriteFixed(instance.xFp);
}

int32_t spritePixelToFixed(int16_t value) {
  return int32_t(value) * SPRITE_SUBPIXEL_SCALE;
}

size_t getSharedSpriteCacheBytes() {
  size_t bytes = 0;
  for (uint8_t index = 0; index < SPRITE_SHARED_CACHE_SLOTS; index++) {
    bytes += spriteSharedCache[index].dataSize;
  }
  return bytes;
}

void clearSharedSpriteCacheEntry(SpriteSharedCacheEntry &entry) {
  if (entry.data) p_free(entry.data);
  entry.assetName[0] = '\0';
  entry.cacheGeneration = 0;
  entry.data = nullptr;
  entry.dataSize = 0;
  entry.lastUsed = 0;
}

void clearSharedSpriteCache() {
  for (uint8_t index = 0; index < SPRITE_SHARED_CACHE_SLOTS; index++) {
    clearSharedSpriteCacheEntry(spriteSharedCache[index]);
  }
}

void setSpriteCacheGeneration(uint32_t generation) {
  if (generation == 0) generation = 1;
  if (spriteCacheGeneration == generation) return;
  spriteCacheGeneration = generation;
  clearSharedSpriteCache();
}

void bumpSpriteCacheGeneration() {
  uint32_t generation = spriteCacheGeneration + 1;
  if (generation == 0) generation = 1;
  setSpriteCacheGeneration(generation);
}

SpriteSharedCacheEntry *findSharedSpriteCacheEntry(const char *assetName) {
  if (!assetName || assetName[0] == '\0') return nullptr;
  for (uint8_t index = 0; index < SPRITE_SHARED_CACHE_SLOTS; index++) {
    SpriteSharedCacheEntry &entry = spriteSharedCache[index];
    if (!entry.data || entry.dataSize == 0) continue;
    if (entry.cacheGeneration != spriteCacheGeneration) {
      clearSharedSpriteCacheEntry(entry);
      continue;
    }
    if (strncmp(entry.assetName, assetName, sizeof(entry.assetName)) == 0) return &entry;
  }
  return nullptr;
}

SpriteSharedCacheEntry *selectSharedSpriteCacheSlot(size_t dataSize) {
  if (dataSize == 0 || dataSize > SPRITE_SHARED_CACHE_BUDGET) return nullptr;

  while (getSharedSpriteCacheBytes() + dataSize > SPRITE_SHARED_CACHE_BUDGET) {
    SpriteSharedCacheEntry *oldest = nullptr;
    for (uint8_t index = 0; index < SPRITE_SHARED_CACHE_SLOTS; index++) {
      SpriteSharedCacheEntry &entry = spriteSharedCache[index];
      if (!entry.data) continue;
      if (!oldest || entry.lastUsed < oldest->lastUsed) oldest = &entry;
    }
    if (!oldest) break;
    clearSharedSpriteCacheEntry(*oldest);
  }

  for (uint8_t index = 0; index < SPRITE_SHARED_CACHE_SLOTS; index++) {
    if (!spriteSharedCache[index].data) return &spriteSharedCache[index];
  }

  SpriteSharedCacheEntry *oldest = &spriteSharedCache[0];
  for (uint8_t index = 1; index < SPRITE_SHARED_CACHE_SLOTS; index++) {
    if (spriteSharedCache[index].lastUsed < oldest->lastUsed) oldest = &spriteSharedCache[index];
  }
  clearSharedSpriteCacheEntry(*oldest);
  return oldest;
}

void rememberSpriteStateInSharedCache(const char *assetName, const Segment &seg, const SpriteState *state) {
  if (!assetName || !state || !seg.data) return;

  const size_t dataSize = seg.dataSize();
  if (dataSize == 0 || dataSize > SPRITE_SHARED_CACHE_BUDGET) return;

  SpriteSharedCacheEntry *entry = findSharedSpriteCacheEntry(assetName);
  if (entry && entry->dataSize != dataSize) {
    clearSharedSpriteCacheEntry(*entry);
    entry = nullptr;
  }
  if (!entry) entry = selectSharedSpriteCacheSlot(dataSize);
  if (!entry) return;

  if (!entry->data) {
    entry->data = static_cast<byte *>(allocate_buffer(dataSize, BFRALLOC_PREFER_PSRAM));
    if (!entry->data) {
      clearSharedSpriteCacheEntry(*entry);
      return;
    }
  }

  memcpy(entry->data, seg.data, dataSize);
  entry->dataSize = dataSize;
  entry->lastUsed = ++spriteSharedCacheClock;
  entry->cacheGeneration = spriteCacheGeneration;
  strlcpy(entry->assetName, assetName, sizeof(entry->assetName));
}

SpriteState *cloneSpriteStateFromSharedCache(Segment &target, const char *assetName) {
  SpriteSharedCacheEntry *entry = findSharedSpriteCacheEntry(assetName);
  if (!entry || !entry->data || entry->dataSize == 0) return nullptr;

  target.deallocateData();
  if (!target.allocateData(entry->dataSize)) return nullptr;

  memcpy(target.data, entry->data, entry->dataSize);
  SpriteState *clone = getSpriteState(target);
  if (!clone) {
    target.deallocateData();
    return nullptr;
  }

  resetSpritePlaybackRuntime(*clone, target);
  clone->lastError = SPRITE_ERROR_NONE;
  entry->lastUsed = ++spriteSharedCacheClock;
  noteSpriteSharedCacheHit();
  return clone;
}

void storeSpriteFailureState(Segment &seg, const char *assetName, byte errorCode) {
  if (!assetName || errorCode == SPRITE_ERROR_NONE) return;

  seg.deallocateData();
  if (!seg.allocateData(sizeof(SpriteState))) return;

  auto *state = reinterpret_cast<SpriteState *>(seg.data);
  memset(state, 0, sizeof(SpriteState));
  state->magic = SPRITE_STATE_MAGIC;
  strlcpy(state->assetName, assetName, sizeof(state->assetName));
  state->cacheGeneration = spriteCacheGeneration;
  state->lastRenderKey = UINT32_MAX;
  state->lastError = errorCode;
}

SpriteState *cloneSpriteStateFromPeer(Segment &target, const char *assetName) {
  if (!assetName || spriteEffectId == 255) return nullptr;

  for (size_t index = 0; index < strip.getSegmentsNum(); index++) {
    Segment &peer = strip.getSegment(index);
    if (&peer == &target || !peer.isActive() || peer.mode != spriteEffectId) continue;

    const SpriteState *source = getSpriteState(peer);
    if (!source || source->lastError != SPRITE_ERROR_NONE || source->frameCount == 0) continue;
    if (source->cacheGeneration != spriteCacheGeneration) continue;
    if (strncmp(source->assetName, assetName, sizeof(source->assetName)) != 0) continue;

    const size_t dataSize = peer.dataSize();
    if (dataSize == 0 || !peer.data) continue;

    target.deallocateData();
    if (!target.allocateData(dataSize)) return nullptr;

    memcpy(target.data, peer.data, dataSize);
    SpriteState *clone = getSpriteState(target);
    if (!clone) {
      target.deallocateData();
      return nullptr;
    }

    resetSpritePlaybackRuntime(*clone, target);
    clone->lastError = SPRITE_ERROR_NONE;
    noteSpriteCacheClone();
    return clone;
  }

  return nullptr;
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

bool spriteUsesMovingLifecycle(const Segment &seg, uint16_t segmentWidth, uint16_t spriteWidth) {
  return seg.speed > 0 && segmentWidth > 0 && spriteWidth <= segmentWidth;
}

uint16_t spritePixelsPerSecond(uint8_t speed) {
  if (speed == 0) return 0;
  // sx=1 should already feel like a very gentle drift. Top end stays fast
  // enough for storm-level wind without making low wind look twitchy.
  return map(speed, 1, 255, 1, 128);
}

uint32_t computeSpriteTravelMs(uint8_t speed, uint16_t segmentWidth, uint16_t spriteWidth) {
  const uint16_t pixelsPerSecond = spritePixelsPerSecond(speed);
  if (pixelsPerSecond == 0) return 0;
  const uint32_t travelPixels = uint32_t(segmentWidth) + uint32_t(spriteWidth);
  return max<uint32_t>(1U, (travelPixels * 1000U + pixelsPerSecond - 1U) / pixelsPerSecond);
}

uint32_t computeSpriteSpawnDelayWindowMs(const Segment &seg, uint32_t travelMs, uint16_t instanceCount) {
  if (seg.custom1 == 0 || travelMs == 0 || instanceCount == 0) return 0;
  const uint32_t spacingMs = max<uint32_t>(250U, travelMs / max<uint16_t>(1, instanceCount));
  return (spacingMs * uint32_t(seg.custom1)) / 255U;
}

void scheduleMovingSpriteInstance(SpriteState &state, SpriteInstance &instance, const Segment &seg,
                                  uint16_t segmentWidth, uint16_t segmentHeight,
                                  uint16_t spriteWidth, uint16_t spriteHeight,
                                  uint16_t instanceCount, bool seedInFlight) {
  const uint16_t maxY = (segmentHeight > spriteHeight) ? segmentHeight - spriteHeight : 0;
  const uint32_t travelMs = computeSpriteTravelMs(seg.speed, segmentWidth, spriteWidth);
  const uint32_t delayWindowMs = computeSpriteSpawnDelayWindowMs(seg, travelMs, instanceCount);

  instance.xFp = -int32_t(spriteWidth) * SPRITE_SUBPIXEL_SCALE;
  instance.y = int16_t(spriteStateRandom(state, uint32_t(maxY) + 1U));

  if (seedInFlight && travelMs > 0) {
    const uint32_t cycleMs = max<uint32_t>(1U, travelMs + delayWindowMs);
    const uint32_t phaseMs = spriteStateRandom(state, cycleMs);
    instance.spawnTime = phaseMs < travelMs
      ? strip.now - phaseMs
      : strip.now + (cycleMs - phaseMs);
    return;
  }

  const uint32_t delayMs = delayWindowMs ? spriteStateRandom(state, delayWindowMs + 1U) : 0;
  instance.spawnTime = strip.now + delayMs;
}

void generateMovingInstanceLayout(SpriteState &state, const Segment &seg, uint16_t requestedCount,
                                  uint16_t segmentWidth, uint16_t segmentHeight,
                                  uint16_t spriteWidth, uint16_t spriteHeight,
                                  uint32_t layoutKey) {
  SpriteInstance *instances = getSpriteInstances(&state);
  state.layoutKey = layoutKey;
  state.instanceCount = requestedCount;
  for (uint16_t index = 0; index < state.instanceCount; index++) {
    scheduleMovingSpriteInstance(state, instances[index], seg, segmentWidth, segmentHeight,
                                 spriteWidth, spriteHeight, state.instanceCount, true);
  }
}

void generateInstanceLayout(SpriteState &state, uint16_t requestedCount, uint16_t segmentWidth, uint16_t segmentHeight, uint16_t spriteWidth, uint16_t spriteHeight, uint32_t layoutKey) {
  SpriteInstance *instances = getSpriteInstances(&state);
  const int16_t centeredX = (int16_t(segmentWidth) - int16_t(spriteWidth)) / 2;
  const int16_t centeredY = (int16_t(segmentHeight) - int16_t(spriteHeight)) / 2;

  state.layoutKey = layoutKey;
  state.instanceCount = 1;
  instances[0] = { spritePixelToFixed(centeredX), centeredY, 0 };

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
      const int32_t dx = candidateCenterX - (spriteInstancePixelX(instances[index]) + spriteHalfWidth);
      const int32_t dy = candidateCenterY - (instances[index].y + spriteHalfHeight);
      if (uint32_t(dx * dx + dy * dy) < minDistanceSq) {
        valid = false;
        break;
      }
    }

    if (valid) {
      instances[state.instanceCount++] = { spritePixelToFixed(candidateX), candidateY, 0 };
      continue;
    }

    attempts++;
    if (attempts % relaxEvery == 0 && minDistance > 1) {
      minDistance = max<uint16_t>(1, uint16_t((uint32_t(minDistance) * 7U) / 8U));
      minDistanceSq = uint32_t(minDistance) * minDistance;
    }
  }

  while (state.instanceCount < requestedCount) {
    const int16_t x = int16_t(prng.random16(maxX + 1));
    const int16_t y = int16_t(prng.random16(maxY + 1));
    instances[state.instanceCount++] = { spritePixelToFixed(x), y, 0 };
  }
}

void ensureInstanceLayout(SpriteState &state, const Segment &seg, uint16_t spriteWidth, uint16_t spriteHeight) {
  const uint16_t segmentWidth = seg.vWidth();
  const uint16_t segmentHeight = seg.vHeight();
  const uint16_t desiredCount = getDesiredInstanceCount(seg, segmentWidth, segmentHeight, spriteWidth, spriteHeight);
  const bool movingLifecycle = spriteUsesMovingLifecycle(seg, segmentWidth, spriteWidth);
  uint32_t layoutKey = hashString32(state.assetName);
  layoutKey ^= uint32_t(segmentWidth) << 20;
  layoutKey ^= uint32_t(segmentHeight) << 12;
  layoutKey ^= uint32_t(spriteWidth) << 6;
  layoutKey ^= uint32_t(spriteHeight);
  layoutKey ^= uint32_t(desiredCount) << 26;
  if (movingLifecycle) {
    layoutKey ^= 0xA5C3F17BUL;
    layoutKey ^= uint32_t(seg.speed) << 1;
    layoutKey ^= uint32_t(seg.custom1) << 9;
  }

  if (state.layoutKey == layoutKey && state.instanceCount == desiredCount) return;
  if (movingLifecycle) {
    generateMovingInstanceLayout(state, seg, desiredCount, segmentWidth, segmentHeight, spriteWidth, spriteHeight, layoutKey);
    return;
  }
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
  state->cacheGeneration = spriteCacheGeneration;
  state->frameCount = frameCount;
  state->pixelDataOffset = metadataBytes;
  state->cacheBytes = pixelBytes;
  state->lastRenderKey = UINT32_MAX;
  state->lastError = SPRITE_ERROR_NONE;
  resetSpritePlaybackRuntime(*state, seg);

  SpriteFrame *frames = getSpriteFrames(state);
  uint32_t dataOffset = 0;
  for (uint16_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    frames[frameIndex].durationMs = specs[frameIndex].durationMs;
    frames[frameIndex].width = specs[frameIndex].width;
    frames[frameIndex].height = specs[frameIndex].height;
    frames[frameIndex].dataOffset = dataOffset;
    frames[frameIndex].flipX = specs[frameIndex].flipX;
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
    context.flipX = frames[frameIndex].flipX;

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

bool readSpriteFileExact(File &file, void *buffer, size_t bytes) {
  return bytes == 0 || file.read(static_cast<uint8_t *>(buffer), bytes) == bytes;
}

byte loadPackedSprite(Segment &seg, const char *assetName, SpriteState *&state) {
  const uint32_t start = millis();
  File file = WLED_FS.open(assetName, "r");
  if (!file) return SPRITE_ERROR_FILE_MISSING;

  SpritePackedHeader header;
  if (!readSpriteFileExact(file, &header, sizeof(header))) {
    file.close();
    return SPRITE_ERROR_BAD_PACKED;
  }

  if (memcmp(header.magic, SPRITE_PACKED_MAGIC, sizeof(header.magic)) != 0
      || header.version != SPRITE_PACKED_VERSION
      || header.frameCount == 0
      || header.frameCount > SPRITE_MAX_FRAMES
      || header.maxWidth == 0
      || header.maxHeight == 0
      || header.maxWidth > SPRITE_MAX_DIMENSION
      || header.maxHeight > SPRITE_MAX_DIMENSION
      || header.pixelBytes > SPRITE_CACHE_BUDGET) {
    file.close();
    return header.pixelBytes > SPRITE_CACHE_BUDGET ? SPRITE_ERROR_TOO_LARGE : SPRITE_ERROR_BAD_PACKED;
  }

  const size_t expectedSize = sizeof(SpritePackedHeader)
    + size_t(header.frameCount) * sizeof(SpritePackedFrame)
    + header.pixelBytes;
  if (file.size() != expectedSize) {
    file.close();
    return SPRITE_ERROR_BAD_PACKED;
  }

  auto *specs = static_cast<SpriteFrameSpec *>(calloc(header.frameCount, sizeof(SpriteFrameSpec)));
  if (!specs) {
    file.close();
    return SPRITE_ERROR_ALLOC;
  }

  uint32_t expectedOffset = 0;
  uint16_t maxWidth = 0;
  uint16_t maxHeight = 0;
  byte errorCode = SPRITE_ERROR_NONE;
  for (uint16_t frameIndex = 0; frameIndex < header.frameCount; frameIndex++) {
    SpritePackedFrame packedFrame;
    if (!readSpriteFileExact(file, &packedFrame, sizeof(packedFrame))) {
      errorCode = SPRITE_ERROR_BAD_PACKED;
      break;
    }

    const uint32_t frameBytes = uint32_t(packedFrame.width) * packedFrame.height * sizeof(SpritePixel);
    if (packedFrame.width == 0
        || packedFrame.height == 0
        || packedFrame.width > SPRITE_MAX_DIMENSION
        || packedFrame.height > SPRITE_MAX_DIMENSION
        || packedFrame.dataOffset != expectedOffset
        || expectedOffset > header.pixelBytes
        || frameBytes > header.pixelBytes - expectedOffset) {
      errorCode = SPRITE_ERROR_BAD_PACKED;
      break;
    }

    specs[frameIndex].durationMs = header.frameCount > 1 ? max<uint16_t>(1, packedFrame.durationMs) : packedFrame.durationMs;
    specs[frameIndex].width = packedFrame.width;
    specs[frameIndex].height = packedFrame.height;
    specs[frameIndex].flipX = false;
    strlcpy(specs[frameIndex].filename, assetName, sizeof(specs[frameIndex].filename));
    expectedOffset += frameBytes;
    if (packedFrame.width > maxWidth) maxWidth = packedFrame.width;
    if (packedFrame.height > maxHeight) maxHeight = packedFrame.height;
  }

  if (errorCode == SPRITE_ERROR_NONE && expectedOffset != header.pixelBytes) {
    errorCode = SPRITE_ERROR_BAD_PACKED;
  }

  if (errorCode == SPRITE_ERROR_NONE && !allocateSpriteState(seg, assetName, specs, header.frameCount, header.pixelBytes, state)) {
    errorCode = SPRITE_ERROR_ALLOC;
  }

  if (errorCode == SPRITE_ERROR_NONE && !readSpriteFileExact(file, getSpritePixelBlob(state), header.pixelBytes)) {
    seg.deallocateData();
    state = nullptr;
    errorCode = SPRITE_ERROR_BAD_PACKED;
  }

  file.close();
  free(specs);

  if (errorCode != SPRITE_ERROR_NONE) return errorCode;

  rememberSpriteStateInSharedCache(assetName, seg, state);
  noteSpriteLoad(maxWidth, maxHeight, header.frameCount, header.pixelBytes, millis() - start);
  return SPRITE_ERROR_NONE;
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

  rememberSpriteStateInSharedCache(assetName, seg, state);
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
    specs[frameIndex].flipX = (frameJson["flip"] | false) || (frameJson["flipX"] | false) || (frameJson["flipped"] | false);
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
  if (state && state->cacheGeneration == spriteCacheGeneration && strncmp(state->assetName, assetName, sizeof(state->assetName)) == 0) {
    return state->lastError == SPRITE_ERROR_NONE ? SPRITE_ERROR_NONE : state->lastError;
  }

  state = cloneSpriteStateFromPeer(seg, assetName);
  if (state) return SPRITE_ERROR_NONE;

  state = cloneSpriteStateFromSharedCache(seg, assetName);
  if (state) return SPRITE_ERROR_NONE;

  seg.deallocateData();
  state = nullptr;

  byte errorCode = SPRITE_ERROR_UNSUPPORTED;
  if (endsWithIgnoreCase(assetName, ".spr")) {
    errorCode = loadPackedSprite(seg, assetName, state);
  } else if (endsWithIgnoreCase(assetName, ".png")) {
    errorCode = loadSingleSprite(seg, assetName, state);
  } else if (endsWithIgnoreCase(assetName, ".json")) {
    errorCode = loadAnimDef(seg, assetName, state);
  }

  if (state) state->lastError = errorCode;
  else if (errorCode != SPRITE_ERROR_NONE) storeSpriteFailureState(seg, assetName, errorCode);
  noteSpriteError(errorCode);
  return errorCode;
}

uint16_t currentFrameDuration(const SpriteState *state) {
  if (!state || state->frameCount == 0) return 0;
  const SpriteFrame *frames = getSpriteFrames(state);
  return frames[state->currentFrame].durationMs;
}

uint16_t initialAnimationFrameOffset(const Segment &seg, const SpriteState &state) {
  if (state.frameCount <= 1 || seg.custom1 == 0) return 0;

  const uint16_t maxOffset = state.frameCount - 1;
  const uint16_t window = 1 + map(uint16_t(seg.custom1), 1, 255, 0, maxOffset);
  uint32_t seed = hashString32(state.assetName);
  seed ^= uint32_t(seg.start) << 20;
  seed ^= uint32_t(seg.stop) << 12;
  seed ^= uint32_t(seg.startY) << 6;
  seed ^= uint32_t(seg.stopY);
  seed ^= uint32_t(state.frameCount) << 27;
  SpritePrng prng(seed);
  return prng.random16(window);
}

inline uint32_t rotl32(uint32_t value, uint8_t shift) {
  return (value << shift) | (value >> (32 - shift));
}

uint32_t buildRenderKey(const Segment &seg, const SpriteState &state, uint32_t motionOffset) {
  uint32_t key = state.layoutKey;
  key ^= uint32_t(state.currentFrame) << 24;
  key ^= motionOffset << 8;
  key ^= motionOffset >> 11;
  key ^= uint32_t(seg.check1) << 2;
  key ^= uint32_t(seg.check2) << 3;
  key ^= uint32_t(seg.check3) << 4;
  key ^= uint32_t(seg.palette) << 16;
  key ^= rotl32(seg.colors[0], 5);
  key ^= rotl32(seg.colors[1], 13);
  key ^= rotl32(seg.colors[2], 21);
  return key;
}

void updateAnimationFrame(const Segment &seg, SpriteState *state) {
  if (!state || state->frameCount <= 1) return;

  if (state->nextFrameTime == 0) {
    state->currentFrame = initialAnimationFrameOffset(seg, *state);
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

uint32_t alphaOverSpriteScratch(uint32_t dstColor, uint32_t srcColor, uint8_t srcAlpha) {
  if (srcAlpha == 0) return dstColor;
  const uint8_t dstAlpha = W(dstColor);
  if (srcAlpha == 255 || dstAlpha == 0) {
    return RGBW32(R(srcColor), G(srcColor), B(srcColor), srcAlpha);
  }

  const uint32_t invSrcAlpha = 255U - srcAlpha;
  const uint32_t srcPremul = uint32_t(srcAlpha) * 255U;
  const uint32_t dstPremul = uint32_t(dstAlpha) * invSrcAlpha;
  const uint32_t outPremul = srcPremul + dstPremul;
  if (outPremul == 0) return 0;

  const uint8_t outAlpha = uint8_t(min<uint32_t>(255U, (outPremul + 127U) / 255U));
  const uint8_t outRed = uint8_t((uint32_t(R(srcColor)) * srcPremul + uint32_t(R(dstColor)) * dstPremul + outPremul / 2U) / outPremul);
  const uint8_t outGreen = uint8_t((uint32_t(G(srcColor)) * srcPremul + uint32_t(G(dstColor)) * dstPremul + outPremul / 2U) / outPremul);
  const uint8_t outBlue = uint8_t((uint32_t(B(srcColor)) * srcPremul + uint32_t(B(dstColor)) * dstPremul + outPremul / 2U) / outPremul);
  return RGBW32(outRed, outGreen, outBlue, outAlpha);
}

void blendSpritePixel(const SpriteRenderContext &context, int16_t destX, int16_t destY, uint32_t sourceColor, uint8_t alpha, bool overwriteAlphaOver = false) {
  if (alpha == 0) return;
  if ((unsigned)destX >= context.segmentWidth || (unsigned)destY >= context.segmentHeight) return;

  const uint32_t outputColor = context.useMask ? getMaskColor(context, destX, destY) : sourceColor;
  // 17 = alpha-over, 18 = holdout. Both need coverage stored in W: alpha-over composites it to
  // the output, holdout hands it to the next segment as a matte (the segment itself never shows).
  if (context.segment->blendMode == 17 || context.segment->blendMode == 18) {
    const uint32_t src = RGBW32(R(outputColor), G(outputColor), B(outputColor), alpha);
    if (overwriteAlphaOver || alpha == 255) {
      context.segment->setPixelColorXY(destX, destY, src);
      return;
    }
    const uint32_t dst = context.segment->getPixelColorXY(destX, destY);
    context.segment->setPixelColorXY(destX, destY, alphaOverSpriteScratch(dst, src, alpha));
    return;
  }

  context.segment->blendPixelColorXY(destX, destY, outputColor, alpha);
}

void sampleSpritePixelHorizontal(const SpritePixel *row, const SpriteRenderContext &context,
                                 int16_t sourceLeftX, uint16_t leftWeight, uint16_t rightWeight,
                                 uint32_t &color, uint8_t &alpha) {
  uint32_t weightedAlpha = 0;
  uint32_t weightedRed = 0;
  uint32_t weightedGreen = 0;
  uint32_t weightedBlue = 0;

  if (leftWeight > 0 && sourceLeftX >= 0 && sourceLeftX < int16_t(context.spriteWidth)) {
    const SpritePixel &pixel = row[sourceLeftX];
    if (pixel.alpha > 0) {
      const uint32_t weight = uint32_t(leftWeight) * pixel.alpha;
      weightedAlpha += weight;
      if (!context.useMask) {
        const uint32_t color32 = rgb565ToRgb32(pixel.color565);
        weightedRed += uint32_t(R(color32)) * weight;
        weightedGreen += uint32_t(G(color32)) * weight;
        weightedBlue += uint32_t(B(color32)) * weight;
      }
    }
  }

  const int16_t sourceRightX = sourceLeftX + 1;
  if (rightWeight > 0 && sourceRightX >= 0 && sourceRightX < int16_t(context.spriteWidth)) {
    const SpritePixel &pixel = row[sourceRightX];
    if (pixel.alpha > 0) {
      const uint32_t weight = uint32_t(rightWeight) * pixel.alpha;
      weightedAlpha += weight;
      if (!context.useMask) {
        const uint32_t color32 = rgb565ToRgb32(pixel.color565);
        weightedRed += uint32_t(R(color32)) * weight;
        weightedGreen += uint32_t(G(color32)) * weight;
        weightedBlue += uint32_t(B(color32)) * weight;
      }
    }
  }

  if (weightedAlpha == 0) {
    color = 0;
    alpha = 0;
    return;
  }

  alpha = uint8_t(min<uint32_t>(255U, (weightedAlpha + (SPRITE_SUBPIXEL_SCALE / 2)) >> SPRITE_SUBPIXEL_SHIFT));
  if (context.useMask) {
    color = SEGCOLOR(0);
    return;
  }

  color = rgbToRgb32(
    uint8_t((weightedRed + weightedAlpha / 2U) / weightedAlpha),
    uint8_t((weightedGreen + weightedAlpha / 2U) / weightedAlpha),
    uint8_t((weightedBlue + weightedAlpha / 2U) / weightedAlpha)
  );
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
    const uint16_t destX = context->flipX ? uint16_t(context->width - 1 - pixelX) : uint16_t(pixelX);
    row[destX].color565 = rgb32To565(color);
    row[destX].alpha = alpha;
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

uint32_t computeMotionOffset(uint8_t speed, uint16_t segmentWidth, uint16_t spriteWidth) {
  if (speed == 0 || segmentWidth == 0 || spriteWidth > segmentWidth) return 0;
  const uint16_t pixelsPerSecond = spritePixelsPerSecond(speed);
  const uint32_t cycleWidthFp = uint32_t(segmentWidth + spriteWidth) * SPRITE_SUBPIXEL_SCALE;
  return ((strip.now * uint32_t(pixelsPerSecond) * SPRITE_SUBPIXEL_SCALE) / 1000U) % max<uint32_t>(1U, cycleWidthFp);
}

void updateMovingSpriteInstances(Segment &seg, SpriteState &state, uint16_t segmentWidth, uint16_t segmentHeight,
                                 uint16_t spriteWidth, uint16_t spriteHeight) {
  if (!spriteUsesMovingLifecycle(seg, segmentWidth, spriteWidth)) return;

  const uint16_t pixelsPerSecond = spritePixelsPerSecond(seg.speed);
  const uint32_t travelMs = computeSpriteTravelMs(seg.speed, segmentWidth, spriteWidth);
  if (pixelsPerSecond == 0 || travelMs == 0) return;

  SpriteInstance *instances = getSpriteInstances(&state);
  for (uint16_t index = 0; index < state.instanceCount; index++) {
    SpriteInstance &instance = instances[index];
    const int32_t elapsedMs = int32_t(strip.now - instance.spawnTime);
    if (elapsedMs < 0) {
      instance.xFp = -int32_t(spriteWidth) * SPRITE_SUBPIXEL_SCALE;
      continue;
    }

    if (uint32_t(elapsedMs) >= travelMs) {
      scheduleMovingSpriteInstance(state, instance, seg, segmentWidth, segmentHeight,
                                   spriteWidth, spriteHeight, state.instanceCount, false);
      continue;
    }

    const int32_t traveledFp = int32_t((uint32_t(elapsedMs) * pixelsPerSecond * SPRITE_SUBPIXEL_SCALE) / 1000U);
    instance.xFp = traveledFp - int32_t(spriteWidth) * SPRITE_SUBPIXEL_SCALE;
  }
}

byte drawSpriteFrame(Segment &seg, SpriteState &state, const SpriteFrame &frame) {
  ensureInstanceLayout(state, seg, state.maxWidth, state.maxHeight);
  const uint32_t motionOffset = computeMotionOffset(seg.speed, seg.vWidth(), state.maxWidth);

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
  context.moving = spriteUsesMovingLifecycle(seg, context.segmentWidth, state.maxWidth);
  if (context.moving) {
    updateMovingSpriteInstances(seg, state, context.segmentWidth, context.segmentHeight, state.maxWidth, state.maxHeight);
  }
  context.instances = getSpriteInstances(&state);

  const uint32_t renderKey = buildRenderKey(seg, state, motionOffset);
  if (renderKey == state.lastRenderKey) {
    noteSpriteDraw(0, 0, true);
    return SPRITE_ERROR_NONE;
  }

  const uint32_t drawStart = micros();
  uint32_t blitPixels = 0;
  // Always clear the segment buffer before drawing the new frame. The segment
  // buffer is the sprite's own scratchpad — WLED composites it over the panel
  // framebuffer (which already holds the sky/background) using the segment's
  // blend mode. Without this clear, pixels NOT touched by the current sprite
  // retain values from prior renders or prior sprite assignments — manifests
  // as residual ghosts (e.g. an old status-icon sprite showing through under a
  // newly-assigned tree sprite, where the tree's alpha=0 pixels skip writes).
  context.segment->fill(0);
  const auto *pixels = reinterpret_cast<const SpritePixel *>(getSpritePixelBlob(&state) + frame.dataOffset);

  if (context.moving) {
    for (uint16_t instanceIndex = 0; instanceIndex < context.instanceCount; instanceIndex++) {
      const SpriteInstance &instance = context.instances[instanceIndex];
      const int16_t drawX = spriteInstancePixelX(instance);
      if (drawX >= int16_t(context.segmentWidth) || drawX + int16_t(context.spriteWidth) < 0) continue;

      const uint16_t rightWeight = uint16_t(SPRITE_SUBPIXEL_SCALE - (uint32_t(instance.xFp) & (SPRITE_SUBPIXEL_SCALE - 1)));
      const uint16_t leftWeight = SPRITE_SUBPIXEL_SCALE - rightWeight;
      const int16_t drawY = instance.y;

      for (uint16_t y = 0; y < frame.height; y++) {
        const SpritePixel *row = pixels + size_t(y) * frame.width;
        const int16_t destY = drawY + y;
        for (uint16_t destOffset = 0; destOffset <= context.spriteWidth; destOffset++) {
          const int16_t destX = drawX + destOffset;
          uint32_t color = 0;
          uint8_t alpha = 0;
          sampleSpritePixelHorizontal(row, context, int16_t(destOffset) - 1, leftWeight, rightWeight, color, alpha);
          if (alpha == 0) continue;
          blendSpritePixel(context, destX, destY, color, alpha, instanceIndex == 0);
          blitPixels++;
        }
      }
    }
  } else {
    for (uint16_t y = 0; y < frame.height; y++) {
      const SpritePixel *row = pixels + size_t(y) * frame.width;
      for (uint16_t x = 0; x < frame.width; x++) {
        if (row[x].alpha == 0) continue;
        const uint32_t color = context.useMask ? SEGCOLOR(0) : rgb565ToRgb32(row[x].color565);
        for (uint16_t instanceIndex = 0; instanceIndex < context.instanceCount; instanceIndex++) {
          const int16_t drawX = spriteInstancePixelX(context.instances[instanceIndex]);
          const int16_t drawY = context.instances[instanceIndex].y;
          const int16_t destX = drawX + x;
          const int16_t destY = drawY + y;
          blendSpritePixel(context, destX, destY, color, row[x].alpha, instanceIndex == 0);
          blitPixels++;
        }
      }
    }
  }

  state.lastRenderKey = renderKey;
  noteSpriteDraw(micros() - drawStart, blitPixels, false);
  return SPRITE_ERROR_NONE;
}


void resetSpriteRuntime(Segment &seg) {
  auto *state = getSpriteState(seg);
  if (!state) return;
  resetSpritePlaybackRuntime(*state, seg);
}

byte renderUsermodSpriteToSegment(Segment &seg) {
  if (!seg.name) return SPRITE_ERROR_NO_NAME;
  if (!strip.isMatrix || !seg.is2D()) return SPRITE_ERROR_UNSUPPORTED;

  SpriteState *state = nullptr;
  const byte stateResult = ensureSpriteState(seg, state);
  if (stateResult != SPRITE_ERROR_NONE) return stateResult;
  if (!state || state->frameCount == 0) return SPRITE_ERROR_BAD_ANIMDEF;

  updateAnimationFrame(seg, state);
  const SpriteFrame *frames = getSpriteFrames(state);
  return drawSpriteFrame(seg, *state, frames[state->currentFrame]);
}

