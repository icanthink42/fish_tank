#include <Arduino.h>
#include "ImageRenderer.h"
#include "MovingFish.h"
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_CO5300.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <math.h>
#include <SensorQMI8658.hpp>
#include <string.h>
#include <touch/TouchDrvCST92xx.h>
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
constexpr int kTouchInt = 11;
constexpr int kTouchReset = 40;

constexpr uint16_t kWaterBlue = RGB565(0, 170, 255);
constexpr size_t kMinFish = 2;
constexpr size_t kMaxFish = 10;
constexpr uint8_t kSpawnChancePercentPerSecond = 5;
constexpr uint32_t kFrameMs = 100;
constexpr size_t kMaxBubbles = 24;
constexpr uint32_t kTouchPollMs = 30;
constexpr uint32_t kImuPollMs = 25;
constexpr uint32_t kPowerButtonPollMs = 100;
constexpr uint32_t kShakeSpinMs = 900;
constexpr float kShakeJerkThresholdG = 0.65f;
constexpr float kBubbleMinGravityProjectionG = 0.12f;
constexpr float kBubbleUpSmoothing = 0.2f;

constexpr uint8_t kAxp2101Address = 0x34;
constexpr uint8_t kAxp2101ChipIdRegister = 0x03;
constexpr uint8_t kAxp2101PowerOffRegister = 0x10;
constexpr uint8_t kAxp2101IrqEnableRegister1 = 0x41;
constexpr uint8_t kAxp2101IrqStatusRegister1 = 0x49;
constexpr uint8_t kAxp2101ChipId = 0x4A;
constexpr uint8_t kAxp2101PowerKeyLongPressIrq = 1 << 2;
constexpr uint8_t kAxp2101PowerKeyShortPressIrq = 1 << 3;
constexpr uint8_t kAxp2101PowerKeyIrqMask =
    kAxp2101PowerKeyShortPressIrq | kAxp2101PowerKeyLongPressIrq;

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

#define DECLARE_BUBBLE_PNG(index)                                              \
  extern const uint8_t bubble##index##PngStart[]                               \
      asm("_binary_assets_bubble_" #index "_png_start");                       \
  extern const uint8_t bubble##index##PngEnd[]                                 \
      asm("_binary_assets_bubble_" #index "_png_end");

DECLARE_BUBBLE_PNG(1)
DECLARE_BUBBLE_PNG(2)
DECLARE_BUBBLE_PNG(3)
DECLARE_BUBBLE_PNG(4)
DECLARE_BUBBLE_PNG(5)
DECLARE_BUBBLE_PNG(6)
DECLARE_BUBBLE_PNG(7)
DECLARE_BUBBLE_PNG(8)

#define FISH_PNG_ENTRY(index)                                                  \
  { fish##index##PngStart, fish##index##PngEnd }
#define BUBBLE_PNG_ENTRY(index)                                                \
  { bubble##index##PngStart, bubble##index##PngEnd }

struct EmbeddedAsset {
  const uint8_t *start;
  const uint8_t *end;
};

constexpr EmbeddedAsset kFishAssets[] = {
    FISH_PNG_ENTRY(1),  FISH_PNG_ENTRY(2),  FISH_PNG_ENTRY(3),
    FISH_PNG_ENTRY(4),  FISH_PNG_ENTRY(5),  FISH_PNG_ENTRY(6),
    FISH_PNG_ENTRY(7),  FISH_PNG_ENTRY(8),  FISH_PNG_ENTRY(9),
    FISH_PNG_ENTRY(10), FISH_PNG_ENTRY(11), FISH_PNG_ENTRY(12),
};
constexpr size_t kFishAssetCount = sizeof(kFishAssets) / sizeof(kFishAssets[0]);

constexpr EmbeddedAsset kBubbleAssets[] = {
    BUBBLE_PNG_ENTRY(1), BUBBLE_PNG_ENTRY(2), BUBBLE_PNG_ENTRY(3),
    BUBBLE_PNG_ENTRY(4), BUBBLE_PNG_ENTRY(5), BUBBLE_PNG_ENTRY(6),
    BUBBLE_PNG_ENTRY(7), BUBBLE_PNG_ENTRY(8),
};
constexpr size_t kBubbleAssetCount =
    sizeof(kBubbleAssets) / sizeof(kBubbleAssets[0]);

Arduino_DataBus *displayBus =
    new Arduino_ESP32QSPI(kLcdCs, kLcdSclk, kLcdSdio0, kLcdSdio1, kLcdSdio2,
                          kLcdSdio3);
Arduino_CO5300 *display =
    new Arduino_CO5300(displayBus, kLcdReset, 0, kLcdWidth, kLcdHeight, 0, 0, 0,
                       0);

struct Bubble {
  bool active = false;
  const Image *image = nullptr;
  float x = 0.0f;
  float y = 0.0f;
  float riseSpeed = 0.0f;
  float wobbleAmp = 0.0f;
  float wobblePhase = 0.0f;
  uint32_t lastUpdateMs = 0;
};

Image fishImages[kFishAssetCount];
bool fishImageLoaded[kFishAssetCount] = {};
Image bubbleImages[kBubbleAssetCount];
bool bubbleImageLoaded[kBubbleAssetCount] = {};
ImageRenderer imageRenderer(*display, kLcdWidth, kLcdHeight);
MovingFish fish[kMaxFish];
bool fishActive[kMaxFish] = {};
Bubble bubbles[kMaxBubbles];
TouchDrvCST92xx touch;
SensorQMI8658 imu;
bool touchReady = false;
bool imuReady = false;
bool pmuReady = false;
uint16_t *frameBuffer = nullptr;
bool fishLoaded = false;
uint32_t lastFrameMs = 0;
uint32_t lastSpawnRollMs = 0;
float bubbleUpX = 0.0f;
float bubbleUpY = -1.0f;
bool bubbleUpReady = false;

void clearFrameBuffer() {
  // Two pixels per 32-bit write; width * height is even.
  constexpr uint32_t kPattern =
      (static_cast<uint32_t>(kWaterBlue) << 16) | kWaterBlue;
  uint32_t *destination = reinterpret_cast<uint32_t *>(frameBuffer);
  const uint32_t count = (static_cast<uint32_t>(kLcdWidth) * kLcdHeight) / 2;
  for (uint32_t i = 0; i < count; ++i) {
    destination[i] = kPattern;
  }
}

// Fast path: no rotation, no scaling. Straight row copy with alpha test.
void drawImageAxisAligned(const Image &image, float x, float y) {
  const int16_t left =
      static_cast<int16_t>(floorf(x - (image.width * 0.5f)));
  const int16_t top =
      static_cast<int16_t>(floorf(y - (image.height * 0.5f)));
  const int16_t srcStartX = left < 0 ? -left : 0;
  const int16_t srcStartY = top < 0 ? -top : 0;
  const int16_t srcEndX =
      left + image.width > kLcdWidth ? kLcdWidth - left : image.width;
  const int16_t srcEndY =
      top + image.height > kLcdHeight ? kLcdHeight - top : image.height;

  for (int16_t srcY = srcStartY; srcY < srcEndY; ++srcY) {
    const uint32_t rowBase = static_cast<uint32_t>(srcY) * image.width;
    const uint16_t *sourceRow = &image.pixels[rowBase + srcStartX];
    const uint8_t *alphaRow =
        image.alpha ? &image.alpha[rowBase + srcStartX] : nullptr;
    uint16_t *destination =
        &frameBuffer[((top + srcY) * kLcdWidth) + left + srcStartX];
    const int16_t count = srcEndX - srcStartX;
    for (int16_t i = 0; i < count; ++i) {
      if (!alphaRow || alphaRow[i]) {
        destination[i] = sourceRow[i];
      }
    }
  }
}

void drawImageToFrameBuffer(const Image &image, float x, float y,
                            float rotationRadians, float scale) {
  if (!frameBuffer || !image.pixels || scale <= 0.0f) {
    return;
  }

  if (fabsf(rotationRadians) < 0.0001f && fabsf(scale - 1.0f) < 0.0001f) {
    drawImageAxisAligned(image, x, y);
    return;
  }

  const float halfW = image.width * scale * 0.5f;
  const float halfH = image.height * scale * 0.5f;
  const float cosA = cosf(rotationRadians);
  const float sinA = sinf(rotationRadians);

  // Tight axis-aligned bounds of the rotated sprite (not the bounding circle).
  const float extentX = (fabsf(cosA) * halfW) + (fabsf(sinA) * halfH);
  const float extentY = (fabsf(sinA) * halfW) + (fabsf(cosA) * halfH);
  const int16_t left = static_cast<int16_t>(floorf(x - extentX));
  const int16_t top = static_cast<int16_t>(floorf(y - extentY));
  const int16_t right = static_cast<int16_t>(ceilf(x + extentX));
  const int16_t bottom = static_cast<int16_t>(ceilf(y + extentY));
  if (right < 0 || bottom < 0 || left >= kLcdWidth || top >= kLcdHeight) {
    return;
  }

  const int16_t clippedLeft = left < 0 ? 0 : left;
  const int16_t clippedTop = top < 0 ? 0 : top;
  const int16_t clippedRight = right >= kLcdWidth ? kLcdWidth - 1 : right;
  const int16_t clippedBottom = bottom >= kLcdHeight ? kLcdHeight - 1 : bottom;
  const float invScale = 1.0f / scale;
  const float imageCenterX = image.width * 0.5f;
  const float imageCenterY = image.height * 0.5f;

  // 16.16 fixed-point incremental mapping: source coordinates change by a
  // constant delta per screen pixel, so the inner loop is integer-only.
  constexpr float kFixedOne = 65536.0f;
  const int32_t stepSrcX = static_cast<int32_t>(cosA * invScale * kFixedOne);
  const int32_t stepSrcY = static_cast<int32_t>(-sinA * invScale * kFixedOne);
  const int32_t rowStepSrcX = static_cast<int32_t>(sinA * invScale * kFixedOne);
  const int32_t rowStepSrcY = static_cast<int32_t>(cosA * invScale * kFixedOne);

  const float startLocalX = (clippedLeft - x) * invScale;
  const float startLocalY = (clippedTop - y) * invScale;
  int32_t rowSrcX = static_cast<int32_t>(
      ((startLocalX * cosA) + (startLocalY * sinA) + imageCenterX) * kFixedOne);
  int32_t rowSrcY = static_cast<int32_t>(
      ((-startLocalX * sinA) + (startLocalY * cosA) + imageCenterY) *
      kFixedOne);

  const int32_t maxSrcX = static_cast<int32_t>(image.width) << 16;
  const int32_t maxSrcY = static_cast<int32_t>(image.height) << 16;

  for (int16_t screenY = clippedTop; screenY <= clippedBottom; ++screenY) {
    uint16_t *destination = &frameBuffer[(screenY * kLcdWidth) + clippedLeft];
    int32_t srcX = rowSrcX;
    int32_t srcY = rowSrcY;
    for (int16_t screenX = clippedLeft; screenX <= clippedRight; ++screenX) {
      if (srcX >= 0 && srcX < maxSrcX && srcY >= 0 && srcY < maxSrcY) {
        const uint32_t sourceIndex =
            static_cast<uint32_t>(srcY >> 16) * image.width +
            static_cast<uint32_t>(srcX >> 16);
        if (!image.alpha || image.alpha[sourceIndex]) {
          *destination = image.pixels[sourceIndex];
        }
      }
      srcX += stepSrcX;
      srcY += stepSrcY;
      ++destination;
    }
    rowSrcX += rowStepSrcX;
    rowSrcY += rowStepSrcY;
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

const Image *ensureAsset(const EmbeddedAsset &asset, Image &image, bool &loaded,
                         const char *label, size_t assetIndex) {
  if (!loaded) {
    EmbeddedPng png;
    png.data = asset.start;
    png.size = static_cast<size_t>(asset.end - asset.start);
    loaded = loadEmbeddedPng(png, image);
    if (!loaded) {
      Serial.printf("failed to load %s asset %u\n", label,
                    static_cast<unsigned>(assetIndex + 1));
      return nullptr;
    }
    Serial.printf("loaded %s asset %u: %dx%d\n", label,
                  static_cast<unsigned>(assetIndex + 1), image.width,
                  image.height);
  }
  return &image;
}

const Image *ensureFishAsset(size_t assetIndex) {
  return ensureAsset(kFishAssets[assetIndex], fishImages[assetIndex],
                     fishImageLoaded[assetIndex], "fish", assetIndex);
}

const Image *ensureBubbleAsset(size_t assetIndex) {
  return ensureAsset(kBubbleAssets[assetIndex], bubbleImages[assetIndex],
                     bubbleImageLoaded[assetIndex], "bubble", assetIndex);
}

void updateBubbleUpFromAccel(float accelX, float accelY) {
  const float gravityScreenX = -accelX;
  const float gravityScreenY = accelY;
  const float gravityMagnitude =
      sqrtf((gravityScreenX * gravityScreenX) +
            (gravityScreenY * gravityScreenY));
  if (gravityMagnitude < kBubbleMinGravityProjectionG) {
    return;
  }

  const float targetUpX = -gravityScreenX / gravityMagnitude;
  const float targetUpY = -gravityScreenY / gravityMagnitude;
  if (!bubbleUpReady) {
    bubbleUpX = targetUpX;
    bubbleUpY = targetUpY;
    bubbleUpReady = true;
    return;
  }

  bubbleUpX =
      (bubbleUpX * (1.0f - kBubbleUpSmoothing)) +
      (targetUpX * kBubbleUpSmoothing);
  bubbleUpY =
      (bubbleUpY * (1.0f - kBubbleUpSmoothing)) +
      (targetUpY * kBubbleUpSmoothing);
  const float smoothedMagnitude =
      sqrtf((bubbleUpX * bubbleUpX) + (bubbleUpY * bubbleUpY));
  if (smoothedMagnitude > 0.0001f) {
    bubbleUpX /= smoothedMagnitude;
    bubbleUpY /= smoothedMagnitude;
  }
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

void spawnBubbles(float tapX, float tapY, uint32_t nowMs) {
  int remaining = random(3, 6);  // 3-5 bubbles
  for (size_t i = 0; i < kMaxBubbles && remaining > 0; ++i) {
    if (bubbles[i].active) {
      continue;
    }
    const size_t assetIndex = random(kBubbleAssetCount);
    const Image *image = ensureBubbleAsset(assetIndex);
    if (!image) {
      return;
    }
    bubbles[i].active = true;
    bubbles[i].image = image;
    bubbles[i].x = tapX + random(-35, 36);
    bubbles[i].y = tapY + random(-35, 36);
    bubbles[i].riseSpeed = static_cast<float>(random(35, 76));
    bubbles[i].wobbleAmp = static_cast<float>(random(2, 9));
    bubbles[i].wobblePhase = random(0, 628) / 100.0f;
    bubbles[i].lastUpdateMs = nowMs;
    --remaining;
  }
}

void updateAndDrawBubbles(uint32_t nowMs) {
  for (size_t i = 0; i < kMaxBubbles; ++i) {
    Bubble &bubble = bubbles[i];
    if (!bubble.active) {
      continue;
    }
    const float elapsedSeconds = (nowMs - bubble.lastUpdateMs) / 1000.0f;
    bubble.lastUpdateMs = nowMs;
    bubble.x += bubbleUpX * bubble.riseSpeed * elapsedSeconds;
    bubble.y += bubbleUpY * bubble.riseSpeed * elapsedSeconds;
    if (bubble.x < -bubble.image->width ||
        bubble.x > kLcdWidth + bubble.image->width ||
        bubble.y < -bubble.image->height ||
        bubble.y > kLcdHeight + bubble.image->height) {
      bubble.active = false;
      continue;
    }
    const float wobble =
        bubble.wobbleAmp *
        sinf((nowMs / 1000.0f * 2.5f) + bubble.wobblePhase);
    const float drawX = bubble.x + (-bubbleUpY * wobble);
    const float drawY = bubble.y + (bubbleUpX * wobble);
    drawImageToFrameBuffer(*bubble.image, drawX, drawY, 0.0f, 1.0f);
  }
}

void pollTouch(uint32_t nowMs) {
  static bool wasTouched = false;
  static uint32_t lastPollMs = 0;
  if (!touchReady || nowMs - lastPollMs < kTouchPollMs) {
    return;
  }
  lastPollMs = nowMs;

  int16_t rawX = 0;
  int16_t rawY = 0;
  const uint8_t touched = touch.getPoint(&rawX, &rawY, 1);
  if (touched > 0 && !wasTouched) {
    // The display is driven with MADCTL 0xA0 (MV|MY) while the touch panel
    // reports native coordinates, so rotate to match the framebuffer.
    // Mapping verified on hardware via corner/edge tap test.
    const float tapX = static_cast<float>(kLcdWidth - 1 - rawY);
    const float tapY = static_cast<float>(rawX);
    spawnBubbles(tapX, tapY, nowMs);
    for (size_t i = 0; i < kMaxFish; ++i) {
      if (fishActive[i]) {
        fish[i].swimTo(tapX, tapY, nowMs);
      }
    }
  }
  wasTouched = touched > 0;
}

bool readPmuRegister(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(kAxp2101Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(kAxp2101Address, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool writePmuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAxp2101Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool updatePmuRegister(uint8_t reg, uint8_t clearMask, uint8_t setMask) {
  uint8_t value = 0;
  if (!readPmuRegister(reg, value)) {
    return false;
  }
  value = (value & ~clearMask) | setMask;
  return writePmuRegister(reg, value);
}

bool initPmuPowerButton() {
  uint8_t chipId = 0;
  if (!readPmuRegister(kAxp2101ChipIdRegister, chipId) ||
      chipId != kAxp2101ChipId) {
    return false;
  }

  if (!updatePmuRegister(kAxp2101IrqEnableRegister1, 0,
                         kAxp2101PowerKeyIrqMask)) {
    return false;
  }
  writePmuRegister(kAxp2101IrqStatusRegister1, kAxp2101PowerKeyIrqMask);
  return true;
}

void enterPowerOff() {
  Serial.println("entering PMU power-off");
  Serial.flush();

  if (imuReady) {
    imu.disableAccelerometer();
    imuReady = false;
  }
  display->setBrightness(0);
  display->displayOff();

  writePmuRegister(kAxp2101IrqStatusRegister1, kAxp2101PowerKeyIrqMask);
  updatePmuRegister(kAxp2101PowerOffRegister, 0, 0x01);
  delay(1000);
}

void pollPowerButton(uint32_t nowMs) {
  static uint32_t lastPollMs = 0;
  if (!pmuReady || nowMs - lastPollMs < kPowerButtonPollMs) {
    return;
  }
  lastPollMs = nowMs;

  uint8_t irqStatus = 0;
  if (!readPmuRegister(kAxp2101IrqStatusRegister1, irqStatus)) {
    return;
  }
  if (irqStatus & kAxp2101PowerKeyIrqMask) {
    writePmuRegister(kAxp2101IrqStatusRegister1,
                     irqStatus & kAxp2101PowerKeyIrqMask);
    enterPowerOff();
  }
}

void triggerShakeSpin(uint32_t nowMs) {
  for (size_t i = 0; i < kMaxFish; ++i) {
    if (fishActive[i]) {
      fish[i].spinInPlace(nowMs, kShakeSpinMs);
    }
  }
}

void pollShake(uint32_t nowMs) {
  static bool havePreviousAccel = false;
  static float previousAccelX = 0.0f;
  static float previousAccelY = 0.0f;
  static float previousAccelZ = 0.0f;
  static uint32_t lastPollMs = 0;

  if (!imuReady || nowMs - lastPollMs < kImuPollMs) {
    return;
  }
  lastPollMs = nowMs;

  float accelX = 0.0f;
  float accelY = 0.0f;
  float accelZ = 0.0f;
  if (!imu.getAccelerometer(accelX, accelY, accelZ)) {
    return;
  }
  updateBubbleUpFromAccel(accelX, accelY);

  if (havePreviousAccel) {
    const float deltaX = accelX - previousAccelX;
    const float deltaY = accelY - previousAccelY;
    const float deltaZ = accelZ - previousAccelZ;
    const float jerkSquared =
        (deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ);
    if (jerkSquared >= kShakeJerkThresholdG * kShakeJerkThresholdG) {
      triggerShakeSpin(nowMs);
    }
  } else {
    havePreviousAccel = true;
  }

  previousAccelX = accelX;
  previousAccelY = accelY;
  previousAccelZ = accelZ;
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
  updateAndDrawBubbles(nowMs);
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

  pmuReady = initPmuPowerButton();
  if (!pmuReady) {
    Serial.println("AXP2101 PMU init failed");
  } else {
    Serial.println("AXP2101 PMU power button ready");
  }

  touch.setPins(kTouchReset, kTouchInt);
  touchReady = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, kI2cSda, kI2cScl);
  if (!touchReady) {
    Serial.println("touch controller init failed");
  } else {
    Serial.printf("touch controller: %s\n", touch.getModelName());
  }

  imuReady = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, kI2cSda, kI2cScl) ||
             imu.begin(Wire, QMI8658_H_SLAVE_ADDRESS, kI2cSda, kI2cScl);
  if (!imuReady) {
    Serial.println("QMI8658 IMU init failed");
  } else {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_250Hz,
                            SensorQMI8658::LPF_MODE_0);
    imu.enableAccelerometer();
    Serial.printf("QMI8658 IMU detected: 0x%02X\n", imu.getChipID());
  }

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
  const uint32_t nowMs = millis();
  pollPowerButton(nowMs);

  if (!fishLoaded) {
    return;
  }

  pollTouch(nowMs);
  pollShake(nowMs);
  if (nowMs - lastFrameMs < kFrameMs) {
    delay(1);
    return;
  }

  lastFrameMs = nowMs;
  drawFrame(nowMs);
}
