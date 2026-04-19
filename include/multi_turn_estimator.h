#pragma once
#include <Arduino.h>

struct MultiTurnEstimator {
  float mt_angle = 0.0f;   // continuous motor angle (rad)
  float prev_raw = 0.0f;
  bool  inited = false;

  void reset(float raw_angle_rad) {
    prev_raw = raw_angle_rad;
    mt_angle = raw_angle_rad;
    inited = true;
  }

  float update(float raw_angle_rad) {
    if (!inited) {
      reset(raw_angle_rad);
      return mt_angle;
    }

    float d = raw_angle_rad - prev_raw;

    // unwrap across 2pi boundary
    if (d > PI)       d -= _2PI;
    else if (d < -PI) d += _2PI;

    mt_angle += d;
    prev_raw = raw_angle_rad;
    return mt_angle;
  }
};
