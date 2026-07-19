#include "ImageRenderer.h"

#include <Arduino.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
PNG pngDecoder;

int cachePngLine(PNGDRAW *draw) {
  Image *image = static_cast<Image *>(draw->pUser);
  if (!image || !image->pixels || !image->alpha || draw->y < 0 ||
      draw->y >= image->height) {
    return 0;
  }

  const uint8_t *source = draw->pPixels;
  const int width = draw->iWidth < image->width ? draw->iWidth : image->width;
  uint16_t *pixelRow = &image->pixels[draw->y * image->width];
  uint8_t *alphaRow = &image->alpha[draw->y * image->width];
  for (int x = 0; x < width; ++x) {
    const uint8_t red = source[0];
    const uint8_t green = source[1];
    const uint8_t blue = source[2];
    const uint8_t alpha = source[3];
    pixelRow[x] = static_cast<uint16_t>(((red & 0xF8) << 8) |
                                        ((green & 0xFC) << 3) | (blue >> 3));
    alphaRow[x] = alpha >= 128 ? 1 : 0;
    source += 4;
  }
  return 1;
}
}

ImageRenderer::ImageRenderer(Arduino_TFT &target, int16_t displayWidth,
                             int16_t displayHeight)
    : target_(target), displayWidth_(displayWidth),
      displayHeight_(displayHeight) {}

ImageRenderer::~ImageRenderer() {}

void ImageRenderer::draw(const Image &image, float x, float y,
                         float rotationRadians, float scale) {
  if (!image.pixels || image.width <= 0 || image.height <= 0 ||
      scale <= 0.0f) {
    return;
  }

  if (fabsf(rotationRadians) < 0.0001f) {
    if (fabsf(scale - 1.0f) < 0.0001f) {
      target_.draw16bitRGBBitmap(roundf(x - (image.width * 0.5f)),
                                 roundf(y - (image.height * 0.5f)),
                                 image.pixels, image.width, image.height);
      return;
    }
  }

  const float halfW = image.width * scale * 0.5f;
  const float halfH = image.height * scale * 0.5f;
  const float radius = sqrtf((halfW * halfW) + (halfH * halfH));
  const int16_t minX = clampToDisplayX(floorf(x - radius));
  const int16_t maxX = clampToDisplayX(ceilf(x + radius));
  const int16_t minY = clampToDisplayY(floorf(y - radius));
  const int16_t maxY = clampToDisplayY(ceilf(y + radius));
  const float cosA = cosf(rotationRadians);
  const float sinA = sinf(rotationRadians);
  const float invScale = 1.0f / scale;
  const float imageCenterX = image.width * 0.5f;
  const float imageCenterY = image.height * 0.5f;

  target_.startWrite();
  for (int16_t screenY = minY; screenY <= maxY; ++screenY) {
    for (int16_t screenX = minX; screenX <= maxX; ++screenX) {
      const float localX = (screenX - x) * invScale;
      const float localY = (screenY - y) * invScale;
      const int16_t sourceX =
          floorf((localX * cosA) + (localY * sinA) + imageCenterX);
      const int16_t sourceY =
          floorf((-localX * sinA) + (localY * cosA) + imageCenterY);

      if (sourceX >= 0 && sourceX < image.width && sourceY >= 0 &&
          sourceY < image.height) {
        target_.writePixel(screenX, screenY,
                           image.pixels[(sourceY * image.width) + sourceX]);
      }
    }
    yield();
  }
  target_.endWrite();
}

int16_t ImageRenderer::clampToDisplayX(float value) const {
  if (value < 0.0f) {
    return 0;
  }
  if (value > static_cast<float>(displayWidth_ - 1)) {
    return displayWidth_ - 1;
  }
  return static_cast<int16_t>(value);
}

int16_t ImageRenderer::clampToDisplayY(float value) const {
  if (value < 0.0f) {
    return 0;
  }
  if (value > static_cast<float>(displayHeight_ - 1)) {
    return displayHeight_ - 1;
  }
  return static_cast<int16_t>(value);
}

bool loadEmbeddedPng(const EmbeddedPng &png, Image &image) {
  if (!png.data || png.size == 0) {
    return false;
  }

  if (pngDecoder.openFLASH(const_cast<uint8_t *>(png.data),
                           static_cast<int>(png.size),
                           cachePngLine) != PNG_SUCCESS) {
    Serial.printf("PNG open failed: %d\n", pngDecoder.getLastError());
    return false;
  }

  image.width = static_cast<int16_t>(pngDecoder.getWidth());
  image.height = static_cast<int16_t>(pngDecoder.getHeight());
  const size_t pixelCount =
      static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  const size_t pixelBytes = pixelCount * sizeof(uint16_t);
  Serial.printf("PNG source: %dx%d, bytes: %u\n", image.width, image.height,
                static_cast<unsigned>(pixelBytes));

  image.pixels =
      static_cast<uint16_t *>(heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM |
                                                              MALLOC_CAP_8BIT));
  if (!image.pixels) {
    image.pixels = static_cast<uint16_t *>(malloc(pixelBytes));
  }
  image.alpha =
      static_cast<uint8_t *>(heap_caps_malloc(pixelCount, MALLOC_CAP_SPIRAM |
                                                              MALLOC_CAP_8BIT));
  if (!image.alpha) {
    image.alpha = static_cast<uint8_t *>(malloc(pixelCount));
  }
  if (!image.pixels || !image.alpha) {
    Serial.printf("Failed to allocate %u bytes for image\n",
                  static_cast<unsigned>(pixelBytes + pixelCount));
    free(image.pixels);
    free(image.alpha);
    image = {};
    pngDecoder.close();
    return false;
  }

  if (pngDecoder.decode(&image, 0) != PNG_SUCCESS) {
    Serial.printf("PNG decode failed: %d\n", pngDecoder.getLastError());
    free(image.pixels);
    free(image.alpha);
    image = {};
    pngDecoder.close();
    return false;
  }

  pngDecoder.close();
  return true;
}
