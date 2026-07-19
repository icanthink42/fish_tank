#pragma once

#include <Arduino_TFT.h>
#include <stddef.h>
#include <stdint.h>

struct Image {
  uint16_t *pixels = nullptr;
  uint8_t *alpha = nullptr;  // 1 = opaque, 0 = transparent
  int16_t width = 0;
  int16_t height = 0;
};

struct EmbeddedPng {
  const uint8_t *data = nullptr;
  size_t size = 0;
};

class ImageRenderer {
public:
  ImageRenderer(Arduino_TFT &target, int16_t displayWidth,
                int16_t displayHeight);
  ~ImageRenderer();

  void draw(const Image &image, float x, float y, float rotationRadians,
            float scale);

private:
  int16_t clampToDisplayX(float value) const;
  int16_t clampToDisplayY(float value) const;

  Arduino_TFT &target_;
  int16_t displayWidth_;
  int16_t displayHeight_;
};

bool loadEmbeddedPng(const EmbeddedPng &png, Image &image);
