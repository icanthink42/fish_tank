#include "MovingFish.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr float kTwoPi = PI * 2.0f;
constexpr float kSeekSpeedMultiplier = 1.5f;
constexpr float kSeekTurnRadiansPerSecond = 3.0f;
constexpr float kSeekArrivalRadius = 40.0f;
constexpr float kMinSpinRadiansPerSecond = 7.0f;
constexpr float kMaxSpinRadiansPerSecond = 12.0f;

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
  entered_ = true;
  x_ = randomX();
  y_ = randomY();
  randomizeHeading();
  state_ = State::Moving;
  stateStartMs_ = nowMs;
  stateDurationMs_ = 0;
  lastUpdateMs_ = nowMs;
  lastPauseRollMs_ = nowMs;
}

void MovingFish::beginOffscreen(const Image &image, float scale,
                                const FishMotionConfig &config,
                                uint32_t nowMs) {
  image_ = &image;
  scale_ = scale;
  config_ = config;
  entered_ = false;

  // Use the rotated bounding radius so no pixel is visible at spawn,
  // regardless of the fish's orientation.
  const float radius = boundingRadius();
  switch (random(4)) {
  case 0:  // left
    x_ = -radius;
    y_ = randomY();
    break;
  case 1:  // right
    x_ = config_.displayWidth + radius;
    y_ = randomY();
    break;
  case 2:  // top
    x_ = randomX();
    y_ = -radius;
    break;
  default:  // bottom
    x_ = randomX();
    y_ = config_.displayHeight + radius;
    break;
  }

  // Aim at a random point in the middle of the screen so the fish swims in.
  const float targetX = config_.displayWidth * (random(25, 76) / 100.0f);
  const float targetY = config_.displayHeight * (random(25, 76) / 100.0f);
  headingRadians_ = atan2f(targetY - y_, targetX - x_);
  targetHeadingRadians_ = headingRadians_;
  rotationRadians_ = headingRadians_ + PI;

  state_ = State::Moving;
  stateStartMs_ = nowMs;
  stateDurationMs_ = 0;
  lastUpdateMs_ = nowMs;
  lastPauseRollMs_ = nowMs;
}

void MovingFish::swimTo(float targetX, float targetY, uint32_t nowMs) {
  if (!image_) {
    return;
  }

  if (state_ == State::Seeking && random(100) >= 25) {
    return;
  }

  seekX_ = targetX;
  seekY_ = targetY;
  state_ = State::Seeking;
  lastUpdateMs_ = nowMs;
  lastPauseRollMs_ = nowMs;
}

void MovingFish::spinInPlace(uint32_t nowMs, uint32_t durationMs) {
  if (!image_) {
    return;
  }

  if (state_ != State::Spinning) {
    const float spinSpeed =
        random(static_cast<long>(kMinSpinRadiansPerSecond * 100.0f),
               static_cast<long>(kMaxSpinRadiansPerSecond * 100.0f) + 1) /
        100.0f;
    spinRadiansPerSecond_ = random(2) == 0 ? spinSpeed : -spinSpeed;
    stateStartMs_ = nowMs;
    stateDurationMs_ = durationMs;
    lastUpdateMs_ = nowMs;
  }

  const uint32_t elapsedMs = nowMs - stateStartMs_;
  if (elapsedMs + durationMs > stateDurationMs_) {
    stateDurationMs_ = elapsedMs + durationMs;
  }
  state_ = State::Spinning;
  lastPauseRollMs_ = nowMs;
}

void MovingFish::update(uint32_t nowMs) {
  if (!image_) {
    return;
  }

  if (state_ == State::Spinning) {
    const float elapsedSeconds = (nowMs - lastUpdateMs_) / 1000.0f;
    lastUpdateMs_ = nowMs;
    rotationRadians_ =
        normalizeRadians(rotationRadians_ +
                         (spinRadiansPerSecond_ * elapsedSeconds));
    if (nowMs - stateStartMs_ >= stateDurationMs_) {
      state_ = State::Moving;
      targetHeadingRadians_ = headingRadians_;
      rotationRadians_ = headingRadians_ + PI;
      lastUpdateMs_ = nowMs;
      lastPauseRollMs_ = nowMs;
    }
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
  if (!entered_ && isFullyVisible()) {
    entered_ = true;
  }
  if (state_ == State::Moving && entered_) {
    updatePauseRoll(nowMs);
  } else {
    // Hold course while entering or seeking; keep the roll clock fresh so
    // the fish doesn't get a burst of turns when it resumes wandering.
    lastPauseRollMs_ = nowMs;
  }
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

  float speed = config_.pixelsPerSecond;
  float maxTurn = config_.maxTurnRadiansPerSecond;
  if (state_ == State::Seeking) {
    targetHeadingRadians_ = atan2f(seekY_ - y_, seekX_ - x_);
    speed *= kSeekSpeedMultiplier;
    maxTurn = kSeekTurnRadiansPerSecond;
  }

  const float turnStep = maxTurn * elapsedSeconds;
  const float headingDelta =
      normalizeRadians(targetHeadingRadians_ - headingRadians_);
  if (fabsf(headingDelta) <= turnStep) {
    headingRadians_ = targetHeadingRadians_;
  } else {
    headingRadians_ += headingDelta > 0.0f ? turnStep : -turnStep;
  }

  x_ += cosf(headingRadians_) * speed * elapsedSeconds;
  y_ += sinf(headingRadians_) * speed * elapsedSeconds;
  rotationRadians_ = headingRadians_ + PI;

  if (state_ == State::Seeking) {
    const float noseOffset = image_->width * scale_ * 0.5f;
    const float noseX = x_ + (cosf(headingRadians_) * noseOffset);
    const float noseY = y_ + (sinf(headingRadians_) * noseOffset);
    const float dx = seekX_ - noseX;
    const float dy = seekY_ - noseY;
    if ((dx * dx) + (dy * dy) <=
        kSeekArrivalRadius * kSeekArrivalRadius) {
      state_ = State::Moving;
      targetHeadingRadians_ = headingRadians_;
    }
  }
}

bool MovingFish::hasEntered() const {
  return entered_;
}

bool MovingFish::isFullyOffscreen() const {
  // Compare against the rotated bounding radius so the fish only counts as
  // gone once no pixel can be on screen in any orientation.
  const float radius = boundingRadius();
  return x_ < -radius || x_ > config_.displayWidth + radius ||
         y_ < -radius || y_ > config_.displayHeight + radius;
}

float MovingFish::boundingRadius() const {
  const float halfWidth = image_->width * scale_ * 0.5f;
  const float halfHeight = image_->height * scale_ * 0.5f;
  return sqrtf((halfWidth * halfWidth) + (halfHeight * halfHeight));
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
