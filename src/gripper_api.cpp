#include "gripper_api.h"

namespace GripperAPI {

volatile float target_open_percent = 0.0f;
float motor_zero_mt_rad = 0.0f;
float as5600_raw_boot_rad = 0.0f;
float as5600_zero_ref_rad = 0.0f;

static float wrapToPi_(float a) {
  while (a > PI) a -= (2.0f * PI);
  while (a < -PI) a += (2.0f * PI);
  return a;
}

static float clampOutputRad_(float x) {
  if (x > GRIP_OUTPUT_MAX_RAD) return GRIP_OUTPUT_MAX_RAD;
  if (x < 0.0f) return 0.0f;
  return x;
}

static float applyInvert_(float pct) {
  if (GRIP_INVERT_DIR) return 100.0f - pct;
  return pct;
}

float clampPercent_(float x) {
  if (x > 100.0f) return 100.0f;
  if (x < 0.0f) return 0.0f;
  return x;
}

void setBootReference(float motor_mt_now_rad, float as5600_boot_rad) {
  // Align output zero to user-defined AS5600 reference.
  float delta = wrapToPi_(as5600_boot_rad - as5600_zero_ref_rad);
  motor_zero_mt_rad   = motor_mt_now_rad - (delta * GEAR_RATIO);
  as5600_raw_boot_rad = as5600_boot_rad;
}

float getTargetOutputAbsRad() {
  float pct = clampPercent_(target_open_percent);
  pct = applyInvert_(pct);
  return (pct * 0.01f) * GRIP_OUTPUT_MAX_RAD;
}

float motorMTToOutputAbsRad(float motor_mt_rad) {
  float out = (motor_mt_rad - motor_zero_mt_rad) / GEAR_RATIO;
  return clampOutputRad_(out);
}

float motorMTToOutputRawRad(float motor_mt_rad) {
  return (motor_mt_rad - motor_zero_mt_rad) / GEAR_RATIO;
}

float motorMTToOutputPercent(float motor_mt_rad) {
  float out = motorMTToOutputAbsRad(motor_mt_rad);
  float pct = (out / GRIP_OUTPUT_MAX_RAD) * 100.0f;
  pct = applyInvert_(pct);
  return pct;
}

float motorMTToOutputPercentRaw(float motor_mt_rad) {
  float out = motorMTToOutputRawRad(motor_mt_rad);
  float pct = (out / GRIP_OUTPUT_MAX_RAD) * 100.0f;
  pct = applyInvert_(pct);
  return pct;
}

float outputToMotorMT(float target_output_abs_rad, float current_motor_mt_rad) {
  float current_output = (current_motor_mt_rad - motor_zero_mt_rad) / GEAR_RATIO;
  float diff = target_output_abs_rad - current_output;
  return current_motor_mt_rad + diff * GEAR_RATIO;
}

} // namespace GripperAPI
