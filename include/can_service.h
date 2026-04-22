#pragma once
#include <Arduino.h>

#include "config/actuator_config.h"

// CAN service layer: policy for decoding commands and reporting position.
namespace CanService {
  bool init(const ActuatorConfig& actuator_config);
  void poll(float current_motor_mt_rad);   // updates active control targets/mode
  bool takePendingOutputProfileChange(OutputEncoderType& out_profile);
  bool takePendingPowerStageEnable(bool& out_enable);
}
