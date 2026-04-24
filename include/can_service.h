#pragma once
#include <Arduino.h>

#include "config/actuator_config.h"

// CAN service layer: policy for decoding commands and reporting position.
namespace CanService {
  bool init(const ActuatorConfig& actuator_config);
  void configure(const ActuatorConfig& actuator_config);
  void poll(float current_motor_mt_rad);   // updates active control targets/mode
  bool takePendingOutputProfileChange(OutputEncoderType& out_profile);
  bool takePendingPowerStageEnable(bool& out_enable);
  bool takePendingActuatorLimitsConfig(float& out_output_min_deg,
                                       float& out_output_max_deg);
  bool takePendingActuatorGearConfig(float& out_gear_ratio);
  bool takePendingOutputEncoderConfig(OutputEncoderType& out_encoder_type,
                                      bool& out_invert);
}
