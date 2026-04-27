#include "config/actuator_defaults.h"

#include <math.h>

#include "board_config.h"

namespace {

constexpr float kFirmwareDefaultOutputMinDeg = ACTUATOR_OUTPUT_DEFAULT_MIN_DEG;
constexpr float kFirmwareDefaultOutputMaxDeg = ACTUATOR_OUTPUT_DEFAULT_MAX_DEG;

bool nearlyEqual_(float a, float b, float epsilon = 0.001f) {
  return fabsf(a - b) <= epsilon;
}

bool actuatorConfigEquals_(const ActuatorConfig& a, const ActuatorConfig& b) {
  return a.version == b.version &&
         a.output_encoder_type == b.output_encoder_type &&
         a.default_control_mode == b.default_control_mode &&
         nearlyEqual_(a.gear_ratio, b.gear_ratio) &&
         a.motor_to_output_sign == b.motor_to_output_sign &&
         nearlyEqual_(a.output_min_deg, b.output_min_deg) &&
         nearlyEqual_(a.output_max_deg, b.output_max_deg) &&
         a.enable_velocity_mode == b.enable_velocity_mode &&
         a.enable_output_angle_mode == b.enable_output_angle_mode &&
         nearlyEqual_(a.voltage_limit, b.voltage_limit) &&
         a.can_node_id == b.can_node_id;
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
      cfg.output_min_deg = 0.0f;
      cfg.output_max_deg = 0.0f;
      cfg.enable_velocity_mode = true;
      cfg.enable_output_angle_mode = false;
      break;

    case OutputEncoderType::DirectInput:
    case OutputEncoderType::As5600:
    case OutputEncoderType::TmagLut:
      cfg.default_control_mode = ControlMode::OutputAngle;
      cfg.output_min_deg = kFirmwareDefaultOutputMinDeg;
      cfg.output_max_deg = kFirmwareDefaultOutputMaxDeg;
      cfg.enable_velocity_mode = true;
      cfg.enable_output_angle_mode = true;
      break;
  }
}

ActuatorConfig buildDefaultActuatorConfig() {
  ActuatorConfig cfg = {};
  cfg.version = 3;
  cfg.gear_ratio = GEAR_RATIO;
  cfg.motor_to_output_sign = MOTOR_TO_OUTPUT_SIGN;
  cfg.output_min_deg = kFirmwareDefaultOutputMinDeg;
  cfg.output_max_deg = kFirmwareDefaultOutputMaxDeg;
  cfg.voltage_limit = TORQUE_LIMIT_VOLTS;
  cfg.can_node_id = CAN_NODE_ID;

#ifndef BUILD_DEFAULT_OUTPUT_ENCODER_TYPE
#define BUILD_DEFAULT_OUTPUT_ENCODER_TYPE 1
#endif
  const OutputEncoderType default_profile =
      static_cast<OutputEncoderType>(BUILD_DEFAULT_OUTPUT_ENCODER_TYPE);
  applyOutputProfileDefaults(cfg, default_profile);

  return cfg;
}

bool syncActuatorConfigToFirmwareDefaults(ActuatorConfig& cfg) {
  const ActuatorConfig firmware_defaults = buildDefaultActuatorConfig();
  if (actuatorConfigEquals_(cfg, firmware_defaults)) {
    return false;
  }

  cfg = firmware_defaults;
  return true;
}
