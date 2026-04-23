#include "actuator_api.h"

#include <math.h>

#include "board_config.h"

namespace ActuatorAPI {

volatile float target_output_deg = 0.0f;
volatile float target_output_velocity_deg_s = 0.0f;
volatile ControlMode active_control_mode = ControlMode::OutputAngle;
float motor_zero_mt_rad = 0.0f;
float output_raw_boot_rad = 0.0f;
float output_zero_ref_rad = 0.0f;

namespace {

constexpr float kDegPerRad = 180.0f / PI;
constexpr float kRadPerDeg = PI / 180.0f;

float gear_ratio_ = GEAR_RATIO;
float output_min_deg_ = 0.0f;
float output_max_deg_ = ACTUATOR_OUTPUT_MAX_DEG;
int8_t motor_to_output_sign_ = 1;
float output_velocity_limit_rad_s_ =
    ACTUATOR_MOTOR_VELOCITY_LIMIT_RAD_S / GEAR_RATIO;

float outputRangeDeg_() {
  const float span = output_max_deg_ - output_min_deg_;
  if (span > 0.0f) return span;
  return 1.0f;
}

float motorToOutputScale_() {
  if (gear_ratio_ <= 0.0f) return 0.0f;
  return static_cast<float>(motor_to_output_sign_) / gear_ratio_;
}

float outputVelocityLimitRad_() {
  if (output_velocity_limit_rad_s_ > 0.0f) return output_velocity_limit_rad_s_;
  return 1.0f;
}

float clampOutputDeg_(float x) {
  if (x > output_max_deg_) return output_max_deg_;
  if (x < output_min_deg_) return output_min_deg_;
  return x;
}

}  // namespace

static float wrapToPi_(float a) {
  while (a > PI) a -= (2.0f * PI);
  while (a < -PI) a += (2.0f * PI);
  return a;
}

void configure(const ActuatorConfig& actuator_config) {
  if (actuator_config.gear_ratio > 0.0f) {
    gear_ratio_ = actuator_config.gear_ratio;
  }
  output_min_deg_ = actuator_config.output_min_deg;
  output_max_deg_ = actuator_config.output_max_deg;
  motor_to_output_sign_ = (actuator_config.motor_to_output_sign >= 0) ? 1 : -1;
  output_velocity_limit_rad_s_ = ACTUATOR_MOTOR_VELOCITY_LIMIT_RAD_S / gear_ratio_;
  active_control_mode = actuator_config.default_control_mode;

  if (output_max_deg_ < output_min_deg_) {
    const float tmp = output_min_deg_;
    output_min_deg_ = output_max_deg_;
    output_max_deg_ = tmp;
  }
}

void setBootReference(float motor_mt_now_rad, float output_boot_rad) {
  const float delta = wrapToPi_(output_boot_rad - output_zero_ref_rad);
  motor_zero_mt_rad =
      motor_mt_now_rad - (delta * static_cast<float>(motor_to_output_sign_) * gear_ratio_);
  output_raw_boot_rad = output_boot_rad;
}

void setBootReferenceFromMotor(float motor_mt_now_rad) {
  motor_zero_mt_rad = motor_mt_now_rad;
  output_raw_boot_rad = 0.0f;
}

float getTargetOutputAbsRad() {
  return target_output_deg * kRadPerDeg;
}

float getTargetOutputDeg() {
  return target_output_deg;
}

float motorMTToOutputAbsRad(float motor_mt_rad) {
  return motorMTToOutputAbsDeg(motor_mt_rad) * kRadPerDeg;
}

float motorMTToOutputRawRad(float motor_mt_rad) {
  return motorMTToOutputRawDeg(motor_mt_rad) * kRadPerDeg;
}

float motorMTToOutputAbsDeg(float motor_mt_rad) {
  const float out_deg = (motor_mt_rad - motor_zero_mt_rad) * motorToOutputScale_() * kDegPerRad;
  return clampOutputDeg_(out_deg);
}

float motorMTToOutputRawDeg(float motor_mt_rad) {
  return (motor_mt_rad - motor_zero_mt_rad) * motorToOutputScale_() * kDegPerRad;
}

float outputDegToMotorMT(float target_output_deg, float current_motor_mt_rad) {
  const float current_output_deg =
      (current_motor_mt_rad - motor_zero_mt_rad) * motorToOutputScale_() * kDegPerRad;
  const float diff_deg = target_output_deg - current_output_deg;
  return current_motor_mt_rad +
         (diff_deg * kRadPerDeg) * static_cast<float>(motor_to_output_sign_) * gear_ratio_;
}

float outputVelocityDegPerSecToMotorVelocity(float velocity_deg_s) {
  float velocity_rad_s = velocity_deg_s * kRadPerDeg;
  const float limit_rad_s = outputVelocityLimitRad_();
  if (velocity_rad_s > limit_rad_s) velocity_rad_s = limit_rad_s;
  if (velocity_rad_s < -limit_rad_s) velocity_rad_s = -limit_rad_s;
  return velocity_rad_s * static_cast<float>(motor_to_output_sign_) * gear_ratio_;
}

float motorVelocityToOutputVelocityDegPerSec(float motor_velocity_rad_s) {
  if (gear_ratio_ <= 0.0f) return 0.0f;
  const float output_velocity_rad_s =
      motor_velocity_rad_s * static_cast<float>(motor_to_output_sign_) / gear_ratio_;
  return output_velocity_rad_s * kDegPerRad;
}

}  // namespace ActuatorAPI
