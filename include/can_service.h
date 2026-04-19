#pragma once
#include <Arduino.h>

// CAN service layer: policy for decoding commands and reporting position.
namespace CanService {
  bool init();
  void poll(float current_motor_mt_rad);   // updates GripperAPI::target_open_percent
  uint32_t lastRxMs();
}
