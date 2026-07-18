#include <Arduino.h>
#include <JPEGDEC.h>
#include "ImageRenderer.h"
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_CO5300.h>
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
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(kI2cSda, kI2cScl);

  Serial.println();
  Serial.println("fish_tank firmware booted");

  if (!display->begin()) {
    Serial.println("display->begin() failed");
    return;
  }

  displayBus->writeC8D8(0x36, 0xA0);
  display->setBrightness(255);
  display->fillScreen(kWaterBlue);

  fishJpeg.data = fishSmallJpgStart;
  fishJpeg.size = static_cast<size_t>(fishSmallJpgEnd - fishSmallJpgStart);
  fishJpeg.decodeScale = JpegDecodeScale::Full;
  Serial.printf("embedded fish JPEG bytes: %u\n",
                static_cast<unsigned>(fishJpeg.size));
  if (loadEmbeddedJpeg(fishJpeg, fishImage)) {
    Serial.println("drawing fish");
    imageRenderer.draw(fishImage, kLcdWidth * 0.5f, kLcdHeight * 0.5f, 0.0f,
                       1.0f);
    Serial.printf("rendered fish image: %dx%d\n", fishImage.width,
                  fishImage.height);
  }
}

void loop() {}
