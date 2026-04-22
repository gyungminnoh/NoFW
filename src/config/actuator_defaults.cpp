#include "config/actuator_defaults.h"

#include <math.h>

#include "board_config.h"

namespace {

constexpr float kLegacyWrongGearRatio = 240.0f;
constexpr float kLegacyWrongOutputMinDeg = 0.0f;
constexpr float kLegacyWrongOutputMaxDeg =
    (ACTUATOR_MOTOR_TURNS * 360.0f) / kLegacyWrongGearRatio;

bool nearlyEqual_(float a, float b, float epsilon = 0.001f) {
  return fabsf(a - b) <= epsilon;
}

}  // namespace

bool isDirectInputCompatible(const ActuatorConfig& config) {
  return fabsf(config.gear_ratio - 1.0f) <= 0.001f;
}

void applyOutputProfileDefaults(ActuatorConfig& cfg, OutputEncoderType profile) {
  cfg.output_encoder_type = profile;

  switch (profile) {
    case OutputEncoderType::VelocityOnly:
      cfg.default_control_mode = ControlMode::OutputVelocity;
      cfg.enable_velocity_mode = true;
      cfg.enable_output_angle_mode = false;
      break;

    case OutputEncoderType::DirectInput:
    case OutputEncoderType::As5600:
    case OutputEncoderType::TmagLut:
      cfg.default_control_mode = ControlMode::OutputAngle;
      cfg.enable_velocity_mode = true;
      cfg.enable_output_angle_mode = true;
      break;
  }
}

ActuatorConfig buildLegacyActuatorConfig() {
  ActuatorConfig cfg = {};
  cfg.version = 2;
  cfg.gear_ratio = GEAR_RATIO;
  cfg.motor_to_output_sign = 1;
  cfg.output_min_deg = 0.0f;
  cfg.output_max_deg = ACTUATOR_OUTPUT_MAX_DEG;
  cfg.can_node_id = CAN_NODE_ID;

  const OutputEncoderType default_profile =
      (GEAR_RATIO <= 1.0f) ? OutputEncoderType::DirectInput
                           : OutputEncoderType::As5600;
  applyOutputProfileDefaults(cfg, default_profile);

  return cfg;
}

bool migrateStaleActuatorConfigDefaults(ActuatorConfig& cfg) {
  if (!nearlyEqual_(cfg.gear_ratio, kLegacyWrongGearRatio)) {
    return false;
  }

  // Migrate only the known stale default fingerprint that shipped with the
  // incorrect compile-time ratio. Do not rewrite arbitrary user-tuned configs.
  if (!nearlyEqual_(cfg.output_min_deg, kLegacyWrongOutputMinDeg) ||
      !nearlyEqual_(cfg.output_max_deg, kLegacyWrongOutputMaxDeg)) {
    return false;
  }

  cfg.gear_ratio = GEAR_RATIO;
  cfg.output_min_deg = 0.0f;
  cfg.output_max_deg = ACTUATOR_OUTPUT_MAX_DEG;
  return true;
}
