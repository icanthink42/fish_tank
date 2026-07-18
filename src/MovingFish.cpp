#include "MovingFish.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr float kTwoPi = PI * 2.0f;

float normalizeRadians(float angle) {
  while (angle > PI) {
    angle -= kTwoPi;
  }
  while (angle < -PI) {
    angle += kTwoPi;
  }
  return angle;
}
}

void MovingFish::begin(const Image &image, float scale,
                       const FishMotionConfig &config, uint32_t nowMs) {
  image_ = &image;
  scale_ = scale;
  config_ = config;
  x_ = randomX();
  y_ = randomY();
  randomizeHeading();
  state_ = State::Moving;
  stateStartMs_ = nowMs;
  stateDurationMs_ = 0;
  lastUpdateMs_ = nowMs;
  lastPauseRollMs_ = nowMs;
}

void MovingFish::update(uint32_t nowMs) {
  if (!image_) {
    return;
  }

  if (state_ == State::Paused) {
    if (nowMs - stateStartMs_ >= stateDurationMs_) {
      state_ = State::Moving;
      lastUpdateMs_ = nowMs;
      lastPauseRollMs_ = nowMs;
      randomizeHeading();
    }
    return;
  }

  updatePosition(nowMs);
  updatePauseRoll(nowMs);
}

void MovingFish::draw(ImageRenderer &renderer) const {
  if (!image_) {
    return;
  }

  renderer.draw(*image_, x_, y_, rotationRadians_, scale_);
}

Rect MovingFish::bounds() const {
  if (!image_) {
    return {};
  }

  const int16_t width = static_cast<int16_t>(ceilf(image_->width * scale_));
  const int16_t height = static_cast<int16_t>(ceilf(image_->height * scale_));
  Rect rect;
  rect.x = static_cast<int16_t>(floorf(x_ - (width * 0.5f))) - 1;
  rect.y = static_cast<int16_t>(floorf(y_ - (height * 0.5f))) - 1;
  rect.width = static_cast<int16_t>(width + 2);
  rect.height = static_cast<int16_t>(height + 2);
  return rect;
}

const Image *MovingFish::image() const {
  return image_;
}

float MovingFish::x() const {
  return x_;
}

float MovingFish::y() const {
  return y_;
}

float MovingFish::rotationRadians() const {
  return rotationRadians_;
}

float MovingFish::scale() const {
  return scale_;
}

void MovingFish::updatePauseRoll(uint32_t nowMs) {
  while (nowMs - lastPauseRollMs_ >= 1000) {
    lastPauseRollMs_ += 1000;
    targetHeadingRadians_ += random(-100, 101) *
                             (config_.maxTurnRadiansPerSecond / 100.0f);

    if (isFullyVisible() &&
        random(100) < config_.pauseChancePercentPerSecond) {
      startPause(nowMs);
      return;
    }
  }
}

void MovingFish::startPause(uint32_t nowMs) {
  state_ = State::Paused;
  stateStartMs_ = nowMs;
  stateDurationMs_ = randomDuration(config_.minPauseMs, config_.maxPauseMs);
}

void MovingFish::updatePosition(uint32_t nowMs) {
  const float elapsedSeconds = (nowMs - lastUpdateMs_) / 1000.0f;
  lastUpdateMs_ = nowMs;
  const float turnStep = config_.maxTurnRadiansPerSecond * elapsedSeconds;
  const float headingDelta =
      normalizeRadians(targetHeadingRadians_ - headingRadians_);
  if (fabsf(headingDelta) <= turnStep) {
    headingRadians_ = targetHeadingRadians_;
  } else {
    headingRadians_ += headingDelta > 0.0f ? turnStep : -turnStep;
  }

  x_ += cosf(headingRadians_) * config_.pixelsPerSecond * elapsedSeconds;
  y_ += sinf(headingRadians_) * config_.pixelsPerSecond * elapsedSeconds;
  wrapAroundBounds();
  rotationRadians_ = headingRadians_ + PI;
}

void MovingFish::wrapAroundBounds() {
  const float halfWidth = image_->width * scale_ * 0.5f;
  const float halfHeight = image_->height * scale_ * 0.5f;
  const float leftExit = -halfWidth;
  const float rightExit = config_.displayWidth + halfWidth;
  const float topExit = -halfHeight;
  const float bottomExit = config_.displayHeight + halfHeight;

  if (x_ < leftExit) {
    x_ = rightExit;
  } else if (x_ > rightExit) {
    x_ = leftExit;
  }

  if (y_ < topExit) {
    y_ = bottomExit;
  } else if (y_ > bottomExit) {
    y_ = topExit;
  }
}

bool MovingFish::isFullyVisible() const {
  const float halfWidth = image_->width * scale_ * 0.5f;
  const float halfHeight = image_->height * scale_ * 0.5f;
  return x_ >= halfWidth && x_ <= config_.displayWidth - halfWidth &&
         y_ >= halfHeight && y_ <= config_.displayHeight - halfHeight;
}

void MovingFish::randomizeHeading() {
  headingRadians_ = random(0, 6284) / 1000.0f;
  targetHeadingRadians_ = headingRadians_;
  rotationRadians_ = headingRadians_ + PI;
}

float MovingFish::randomX() const {
  const float halfWidth = image_->width * scale_ * 0.5f;
  const long minX = static_cast<long>(halfWidth);
  const long maxX = static_cast<long>(config_.displayWidth - halfWidth);
  if (maxX <= minX) {
    return config_.displayWidth * 0.5f;
  }
  return static_cast<float>(random(minX, maxX + 1));
}

float MovingFish::randomY() const {
  const float halfHeight = image_->height * scale_ * 0.5f;
  const long minY = static_cast<long>(halfHeight);
  const long maxY = static_cast<long>(config_.displayHeight - halfHeight);
  if (maxY <= minY) {
    return config_.displayHeight * 0.5f;
  }
  return static_cast<float>(random(minY, maxY + 1));
}

uint32_t MovingFish::randomDuration(uint32_t minMs, uint32_t maxMs) const {
  if (maxMs <= minMs) {
    return minMs;
  }
  return static_cast<uint32_t>(random(minMs, maxMs + 1));
}
