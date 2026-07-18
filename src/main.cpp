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
constexpr size_t kFishCount = 2;
constexpr uint32_t kFrameMs = 100;

extern const uint8_t fishSmallJpgStart[]
    asm("_binary_assets_fish_small_jpg_start");
extern const uint8_t fishSmallJpgEnd[] asm("_binary_assets_fish_small_jpg_end");

Arduino_DataBus *displayBus =
    new Arduino_ESP32QSPI(kLcdCs, kLcdSclk, kLcdSdio0, kLcdSdio1, kLcdSdio2,
                          kLcdSdio3);
Arduino_CO5300 *display =
    new Arduino_CO5300(displayBus, kLcdReset, 0, kLcdWidth, kLcdHeight, 0, 0, 0,
                       0);

Image fishImage;
ImageRenderer imageRenderer(*display, kLcdWidth, kLcdHeight);
EmbeddedJpeg fishJpeg;
MovingFish fish[kFishCount];
uint16_t *frameBuffer = nullptr;
bool fishLoaded = false;
uint32_t lastFrameMs = 0;

void clearFrameBuffer() {
  for (uint32_t i = 0; i < static_cast<uint32_t>(kLcdWidth) * kLcdHeight; ++i) {
    frameBuffer[i] = kWaterBlue;
  }
}

bool isTransparentFishPixel(uint16_t color) {
  const uint8_t red = (color >> 11) & 0x1F;
  const uint8_t green = (color >> 5) & 0x3F;
  const uint8_t blue = color & 0x1F;
  return red > 27 && green > 55 && blue > 27;
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
        const uint16_t color = image.pixels[(sourceY * image.width) + sourceX];
        if (!isTransparentFishPixel(color)) {
          *destination = color;
        }
      }
      ++destination;
    }
  }
}

void presentFrameBuffer() {
  display->draw16bitRGBBitmap(0, 0, frameBuffer, kLcdWidth, kLcdHeight);
}

void drawFrame(uint32_t nowMs) {
  for (size_t i = 0; i < kFishCount; ++i) {
    fish[i].update(nowMs);
  }

  clearFrameBuffer();
  for (size_t i = 0; i < kFishCount; ++i) {
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

  fishJpeg.data = fishSmallJpgStart;
  fishJpeg.size = static_cast<size_t>(fishSmallJpgEnd - fishSmallJpgStart);
  fishJpeg.decodeScale = JpegDecodeScale::Full;
  Serial.printf("embedded fish JPEG bytes: %u\n",
                static_cast<unsigned>(fishJpeg.size));
  fishLoaded = loadEmbeddedJpeg(fishJpeg, fishImage);
  if (fishLoaded) {
    FishMotionConfig motionConfig;
    motionConfig.displayWidth = kLcdWidth;
    motionConfig.displayHeight = kLcdHeight;
    motionConfig.pixelsPerSecond = 35.0f;
    motionConfig.maxTurnRadiansPerSecond = 0.65f;
    motionConfig.pauseChancePercentPerSecond = 5;
    motionConfig.minPauseMs = 5000;
    motionConfig.maxPauseMs = 60000;
    const uint32_t nowMs = millis();
    for (size_t i = 0; i < kFishCount; ++i) {
      fish[i].begin(fishImage, 1.0f, motionConfig, nowMs);
    }
    drawFrame(nowMs);
    lastFrameMs = nowMs;
    Serial.printf("rendered fish image: %dx%d\n", fishImage.width,
                  fishImage.height);
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
