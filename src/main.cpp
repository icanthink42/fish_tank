#include <Arduino.h>
#include "ImageRenderer.h"
#include "MovingFish.h"
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_CO5300.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <math.h>
#include <string.h>
#include <Wire.h>

namespace {
constexpr int kI2cSda = 15;
constexpr int kI2cScl = 14;

constexpr int kLcdSdio0 = 4;
constexpr int kLcdSdio1 = 5;
constexpr int kLcdSdio2 = 6;
constexpr int kLcdSdio3 = 7;
constexpr int kLcdSclk = 38;
constexpr int kLcdReset = 39;
constexpr int kLcdCs = 12;
constexpr int kLcdWidth = 480;
constexpr int kLcdHeight = 480;
constexpr uint16_t kWaterBlue = RGB565(0, 170, 255);
constexpr size_t kMinFish = 2;
constexpr size_t kMaxFish = 10;
constexpr uint8_t kSpawnChancePercentPerSecond = 5;
constexpr uint32_t kFrameMs = 100;

#define DECLARE_FISH_PNG(index)                                                \
  extern const uint8_t fish##index##PngStart[]                                 \
      asm("_binary_assets_fish_" #index "_u_png_start");                       \
  extern const uint8_t fish##index##PngEnd[]                                   \
      asm("_binary_assets_fish_" #index "_u_png_end");

DECLARE_FISH_PNG(1)
DECLARE_FISH_PNG(2)
DECLARE_FISH_PNG(3)
DECLARE_FISH_PNG(4)
DECLARE_FISH_PNG(5)
DECLARE_FISH_PNG(6)
DECLARE_FISH_PNG(7)
DECLARE_FISH_PNG(8)
DECLARE_FISH_PNG(9)
DECLARE_FISH_PNG(10)
DECLARE_FISH_PNG(11)
DECLARE_FISH_PNG(12)

#define FISH_PNG_ENTRY(index)                                                  \
  { fish##index##PngStart, fish##index##PngEnd }

struct FishAsset {
  const uint8_t *start;
  const uint8_t *end;
};

constexpr FishAsset kFishAssets[] = {
    FISH_PNG_ENTRY(1),  FISH_PNG_ENTRY(2),  FISH_PNG_ENTRY(3),
    FISH_PNG_ENTRY(4),  FISH_PNG_ENTRY(5),  FISH_PNG_ENTRY(6),
    FISH_PNG_ENTRY(7),  FISH_PNG_ENTRY(8),  FISH_PNG_ENTRY(9),
    FISH_PNG_ENTRY(10), FISH_PNG_ENTRY(11), FISH_PNG_ENTRY(12),
};
constexpr size_t kFishAssetCount = sizeof(kFishAssets) / sizeof(kFishAssets[0]);

Arduino_DataBus *displayBus =
    new Arduino_ESP32QSPI(kLcdCs, kLcdSclk, kLcdSdio0, kLcdSdio1, kLcdSdio2,
                          kLcdSdio3);
Arduino_CO5300 *display =
    new Arduino_CO5300(displayBus, kLcdReset, 0, kLcdWidth, kLcdHeight, 0, 0, 0,
                       0);

Image fishImages[kFishAssetCount];
bool fishImageLoaded[kFishAssetCount] = {};
ImageRenderer imageRenderer(*display, kLcdWidth, kLcdHeight);
MovingFish fish[kMaxFish];
bool fishActive[kMaxFish] = {};
uint16_t *frameBuffer = nullptr;
bool fishLoaded = false;
uint32_t lastFrameMs = 0;
uint32_t lastSpawnRollMs = 0;

void clearFrameBuffer() {
  for (uint32_t i = 0; i < static_cast<uint32_t>(kLcdWidth) * kLcdHeight; ++i) {
    frameBuffer[i] = kWaterBlue;
  }
}

void drawImageToFrameBuffer(const Image &image, float x, float y,
                            float rotationRadians, float scale) {
  if (!frameBuffer || !image.pixels || scale <= 0.0f) {
    return;
  }

  const float halfW = image.width * scale * 0.5f;
  const float halfH = image.height * scale * 0.5f;
  const float radius = sqrtf((halfW * halfW) + (halfH * halfH));
  const int16_t left = static_cast<int16_t>(floorf(x - radius));
  const int16_t top = static_cast<int16_t>(floorf(y - radius));
  const int16_t right = static_cast<int16_t>(ceilf(x + radius));
  const int16_t bottom = static_cast<int16_t>(ceilf(y + radius));
  if (right < 0 || bottom < 0 || left >= kLcdWidth || top >= kLcdHeight) {
    return;
  }

  const int16_t clippedLeft = left < 0 ? 0 : left;
  const int16_t clippedTop = top < 0 ? 0 : top;
  const int16_t clippedRight = right >= kLcdWidth ? kLcdWidth - 1 : right;
  const int16_t clippedBottom = bottom >= kLcdHeight ? kLcdHeight - 1 : bottom;
  const float cosA = cosf(rotationRadians);
  const float sinA = sinf(rotationRadians);
  const float invScale = 1.0f / scale;
  const float imageCenterX = image.width * 0.5f;
  const float imageCenterY = image.height * 0.5f;

  for (int16_t screenY = clippedTop; screenY <= clippedBottom; ++screenY) {
    uint16_t *destination = &frameBuffer[(screenY * kLcdWidth) + clippedLeft];
    for (int16_t screenX = clippedLeft; screenX <= clippedRight; ++screenX) {
      const float localX = (screenX - x) * invScale;
      const float localY = (screenY - y) * invScale;
      const int16_t sourceX =
          static_cast<int16_t>(floorf((localX * cosA) + (localY * sinA) +
                                      imageCenterX));
      const int16_t sourceY =
          static_cast<int16_t>(floorf((-localX * sinA) + (localY * cosA) +
                                      imageCenterY));
      if (sourceX >= 0 && sourceX < image.width && sourceY >= 0 &&
          sourceY < image.height) {
        const uint32_t sourceIndex = (sourceY * image.width) + sourceX;
        if (!image.alpha || image.alpha[sourceIndex]) {
          *destination = image.pixels[sourceIndex];
        }
      }
      ++destination;
    }
  }
}

void presentFrameBuffer() {
  display->draw16bitRGBBitmap(0, 0, frameBuffer, kLcdWidth, kLcdHeight);
}

size_t activeFishCount() {
  size_t count = 0;
  for (size_t i = 0; i < kMaxFish; ++i) {
    if (fishActive[i]) {
      ++count;
    }
  }
  return count;
}

FishMotionConfig randomMotionConfig() {
  FishMotionConfig motionConfig;
  motionConfig.displayWidth = kLcdWidth;
  motionConfig.displayHeight = kLcdHeight;
  motionConfig.pixelsPerSecond = static_cast<float>(random(20, 61));
  motionConfig.maxTurnRadiansPerSecond = random(35, 111) / 100.0f;
  motionConfig.pauseChancePercentPerSecond =
      static_cast<uint8_t>(random(2, 9));
  motionConfig.minPauseMs = 5000;
  motionConfig.maxPauseMs = 60000;
  return motionConfig;
}

const Image *ensureFishAsset(size_t assetIndex) {
  if (!fishImageLoaded[assetIndex]) {
    EmbeddedPng png;
    png.data = kFishAssets[assetIndex].start;
    png.size = static_cast<size_t>(kFishAssets[assetIndex].end -
                                   kFishAssets[assetIndex].start);
    fishImageLoaded[assetIndex] = loadEmbeddedPng(png, fishImages[assetIndex]);
    if (!fishImageLoaded[assetIndex]) {
      Serial.printf("failed to load fish asset %u\n",
                    static_cast<unsigned>(assetIndex + 1));
      return nullptr;
    }
    Serial.printf("loaded fish asset %u: %dx%d\n",
                  static_cast<unsigned>(assetIndex + 1),
                  fishImages[assetIndex].width, fishImages[assetIndex].height);
  }
  return &fishImages[assetIndex];
}

bool spawnFish(uint32_t nowMs, bool onScreen) {
  for (size_t slot = 0; slot < kMaxFish; ++slot) {
    if (fishActive[slot]) {
      continue;
    }
    const size_t assetIndex = random(kFishAssetCount);
    const Image *image = ensureFishAsset(assetIndex);
    if (!image) {
      return false;
    }
    if (onScreen) {
      fish[slot].begin(*image, 1.0f, randomMotionConfig(), nowMs);
    } else {
      fish[slot].beginOffscreen(*image, 1.0f, randomMotionConfig(), nowMs);
    }
    fishActive[slot] = true;
    return true;
  }
  return false;
}

void updateFishPopulation(uint32_t nowMs) {
  // Despawn fish that swam fully off screen.
  for (size_t i = 0; i < kMaxFish; ++i) {
    if (fishActive[i] && fish[i].hasEntered() && fish[i].isFullyOffscreen()) {
      fishActive[i] = false;
    }
  }

  // Random chance for a new fish to swim in.
  while (nowMs - lastSpawnRollMs >= 1000) {
    lastSpawnRollMs += 1000;
    if (activeFishCount() < kMaxFish &&
        random(100) < kSpawnChancePercentPerSecond) {
      spawnFish(nowMs, false);
    }
  }

  // Keep the minimum population.
  while (activeFishCount() < kMinFish) {
    if (!spawnFish(nowMs, false)) {
      break;
    }
  }
}

void drawFrame(uint32_t nowMs) {
  for (size_t i = 0; i < kMaxFish; ++i) {
    if (fishActive[i]) {
      fish[i].update(nowMs);
    }
  }

  updateFishPopulation(nowMs);

  clearFrameBuffer();
  for (size_t i = 0; i < kMaxFish; ++i) {
    if (!fishActive[i]) {
      continue;
    }
    const Image *image = fish[i].image();
    if (image) {
      drawImageToFrameBuffer(*image, fish[i].x(), fish[i].y(),
                             fish[i].rotationRadians(), fish[i].scale());
    }
  }
  presentFrameBuffer();
}
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(kI2cSda, kI2cScl);
  randomSeed(esp_random());

  Serial.println();
  Serial.println("fish_tank firmware booted");

  if (!display->begin()) {
    Serial.println("display->begin() failed");
    return;
  }

  displayBus->writeC8D8(0x36, 0xA0);
  display->setBrightness(255);
  display->fillScreen(kWaterBlue);
  frameBuffer = static_cast<uint16_t *>(heap_caps_malloc(
      static_cast<uint32_t>(kLcdWidth) * kLcdHeight * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frameBuffer) {
    Serial.println("framebuffer allocation failed");
    return;
  }

  const uint32_t nowMs = millis();
  lastSpawnRollMs = nowMs;
  for (size_t i = 0; i < kMinFish; ++i) {
    spawnFish(nowMs, true);
  }
  fishLoaded = activeFishCount() > 0;
  if (fishLoaded) {
    drawFrame(nowMs);
    lastFrameMs = nowMs;
  }
}

void loop() {
  if (!fishLoaded) {
    return;
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastFrameMs < kFrameMs) {
    delay(1);
    return;
  }

  lastFrameMs = nowMs;
  drawFrame(nowMs);
}
