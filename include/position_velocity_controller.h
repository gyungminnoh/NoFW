#pragma once
#include <Arduino.h>

struct PositionVelocityController {
  // Outer loop: position error -> velocity command
  float Kp = 25.0f;            // rad/s per rad error (tune)
  float vel_limit = 10.0f;    // rad/s clamp
  float accel_limit = 100.0f; // rad/s^2 clamp
  float deadband = 0.002f;    // rad

  float last_v = 0.0f;
  uint32_t last_ms = 0;

  void reset() {
    last_v = 0.0f;
    last_ms = millis();
  }

  float compute(float target_pos, float current_pos) {
    const uint32_t now_ms = millis();
    float dt = 0.0f;
    if (last_ms != 0) {
      dt = (now_ms - last_ms) * 0.001f;
    }
    last_ms = now_ms;

    float err = target_pos - current_pos;
    if (fabsf(err) < deadband) {
      last_v = 0.0f;
      return 0.0f;
    }

    float v = Kp * err;

    // clamp
    if (v > vel_limit) v = vel_limit;
    if (v < -vel_limit) v = -vel_limit;

    // accel limit (slew rate)
    if (dt > 0.0f) {
      const float max_delta = accel_limit * dt;
      const float dv = v - last_v;
      if (dv > max_delta) v = last_v + max_delta;
      if (dv < -max_delta) v = last_v - max_delta;
    }

    last_v = v;
    return v;
  }
};
