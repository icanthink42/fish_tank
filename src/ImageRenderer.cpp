#include "ImageRenderer.h"

#include <Arduino.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
JPEGDEC jpegDecoder;

int16_t minInt16(int16_t left, int16_t right) {
  return left < right ? left : right;
}

int cacheJpegBlock(JPEGDRAW *draw) {
  Image *image = static_cast<Image *>(draw->pUser);
  if (!image || !image->pixels) {
    return 0;
  }

  const int16_t copyWidth =
      minInt16(draw->iWidthUsed, image->width - draw->x);
  const int16_t copyHeight = minInt16(draw->iHeight, image->height - draw->y);
  if (copyWidth <= 0 || copyHeight <= 0 || draw->x < 0 || draw->y < 0) {
    return 1;
  }

  for (int16_t row = 0; row < copyHeight; ++row) {
    memcpy(&image->pixels[((draw->y + row) * image->width) + draw->x],
           &draw->pPixels[row * draw->iWidth], copyWidth * sizeof(uint16_t));
  }

  return 1;
}

int jpegDecodeOptions(JpegDecodeScale scale) {
  switch (scale) {
  case JpegDecodeScale::Half:
    return JPEG_SCALE_HALF;
  case JpegDecodeScale::Quarter:
    return JPEG_SCALE_QUARTER;
  case JpegDecodeScale::Eighth:
    return JPEG_SCALE_EIGHTH;
  case JpegDecodeScale::Full:
  default:
    return 0;
  }
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

bool loadEmbeddedJpeg(const EmbeddedJpeg &jpeg, Image &image) {
  if (!jpeg.data || jpeg.size == 0) {
    return false;
  }

  jpegDecoder.close();
  if (!jpegDecoder.openFLASH(jpeg.data, jpeg.size, cacheJpegBlock)) {
    Serial.printf("JPEG open failed: %d\n", jpegDecoder.getLastError());
    return false;
  }

  const int decodeScale = static_cast<int>(jpeg.decodeScale);
  image.width = (jpegDecoder.getWidth() + decodeScale - 1) / decodeScale;
  image.height = (jpegDecoder.getHeight() + decodeScale - 1) / decodeScale;
  const size_t pixelBytes = image.width * image.height * sizeof(uint16_t);
  Serial.printf("JPEG source: %dx%d, decoded: %dx%d, bytes: %u\n",
                jpegDecoder.getWidth(), jpegDecoder.getHeight(), image.width,
                image.height, static_cast<unsigned>(pixelBytes));

  image.pixels =
      static_cast<uint16_t *>(heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM |
                                                              MALLOC_CAP_8BIT));
  if (!image.pixels) {
    image.pixels = static_cast<uint16_t *>(malloc(pixelBytes));
  }
  if (!image.pixels) {
    Serial.printf("Failed to allocate %u bytes for image\n",
                  static_cast<unsigned>(pixelBytes));
    jpegDecoder.close();
    return false;
  }

  jpegDecoder.setUserPointer(&image);
  if (!jpegDecoder.decode(0, 0, jpegDecodeOptions(jpeg.decodeScale))) {
    Serial.printf("JPEG decode failed: %d\n", jpegDecoder.getLastError());
    free(image.pixels);
    image = {};
    jpegDecoder.close();
    return false;
  }

  jpegDecoder.close();
  return true;
}
