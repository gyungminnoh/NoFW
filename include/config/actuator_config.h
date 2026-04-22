#pragma once

#include <stdint.h>

#include "config/actuator_types.h"

struct ActuatorConfig {
  uint32_t version = 2;

  OutputEncoderType output_encoder_type = OutputEncoderType::As5600;
  ControlMode default_control_mode = ControlMode::OutputAngle;

  float gear_ratio = 1.0f;
  int8_t motor_to_output_sign = 1;

  float output_min_deg = 0.0f;
  float output_max_deg = 0.0f;

  bool enable_velocity_mode = true;
  bool enable_output_angle_mode = true;

  uint8_t can_node_id = 7;
};
