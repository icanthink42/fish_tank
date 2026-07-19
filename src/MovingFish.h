#pragma once

#include "ImageRenderer.h"

#include <stdint.h>

struct FishMotionConfig {
  int16_t displayWidth = 0;
  int16_t displayHeight = 0;
  float pixelsPerSecond = 35.0f;
  float maxTurnRadiansPerSecond = 0.65f;
  uint8_t pauseChancePercentPerSecond = 5;
  uint32_t minPauseMs = 5000;
  uint32_t maxPauseMs = 60000;
};

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;
};

class MovingFish {
public:
  void begin(const Image &image, float scale, const FishMotionConfig &config,
             uint32_t nowMs);
  void beginOffscreen(const Image &image, float scale,
                      const FishMotionConfig &config, uint32_t nowMs);
  void spinInPlace(uint32_t nowMs, uint32_t durationMs);
  void swimTo(float targetX, float targetY, uint32_t nowMs);
  void update(uint32_t nowMs);
  void draw(ImageRenderer &renderer) const;
  Rect bounds() const;
  bool hasEntered() const;
  bool isFullyOffscreen() const;
  const Image *image() const;
  float x() const;
  float y() const;
  float rotationRadians() const;
  float scale() const;

private:
  enum class State {
    Moving,
    Paused,
    Seeking,
    Spinning,
  };

  void updatePauseRoll(uint32_t nowMs);
  void startPause(uint32_t nowMs);
  void updatePosition(uint32_t nowMs);
  bool isFullyVisible() const;
  float boundingRadius() const;
  void randomizeHeading();
  float randomX() const;
  float randomY() const;
  uint32_t randomDuration(uint32_t minMs, uint32_t maxMs) const;

  const Image *image_ = nullptr;
  FishMotionConfig config_;
  State state_ = State::Moving;
  bool entered_ = false;
  float scale_ = 1.0f;
  float x_ = 0.0f;
  float y_ = 0.0f;
  float rotationRadians_ = 0.0f;
  float headingRadians_ = 0.0f;
  float targetHeadingRadians_ = 0.0f;
  float spinRadiansPerSecond_ = 0.0f;
  float seekX_ = 0.0f;
  float seekY_ = 0.0f;
  uint32_t stateStartMs_ = 0;
  uint32_t stateDurationMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  uint32_t lastPauseRollMs_ = 0;
};
