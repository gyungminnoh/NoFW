#pragma once
#include <Arduino.h>
#include "board_config.h"

// ========================================================
// Gripper command interface
// - External command: target_open_percent (0..100)
// - 0% = fully open, 100% = fully closed
// - Output range is limited to [0, GRIP_OUTPUT_MAX_RAD]
// - AS5600 is read once at boot; no runtime output sensor feedback
// ========================================================
namespace GripperAPI {

  // External command (updated by CAN service).
  extern volatile float target_open_percent;

  // Setup-only reference: motor MT angle that corresponds to output "0 rad"
  // in AS5600 absolute coordinates.
  extern float motor_zero_mt_rad;

  // Optional: store the AS5600 raw at boot for debug/diagnostics.
  extern float as5600_raw_boot_rad;

  // AS5600 absolute angle that corresponds to output 0 (user-defined).
  extern float as5600_zero_ref_rad;

  // Called at setup after FOC init and MT reset.
  void setBootReference(float motor_mt_now_rad, float as5600_boot_rad);

  // Output target absolute in boot-centered coordinates (radians).
  float getTargetOutputAbsRad();

  // Convert current motor multi-turn to output angle (clamped).
  float motorMTToOutputAbsRad(float motor_mt_rad);

  // Convert current motor multi-turn to output angle (unclamped).
  float motorMTToOutputRawRad(float motor_mt_rad);

  // Convert current motor multi-turn to output percent (clamped).
  float motorMTToOutputPercent(float motor_mt_rad);

  // Convert current motor multi-turn to output percent (unclamped).
  float motorMTToOutputPercentRaw(float motor_mt_rad);

  // Convert desired output angle (abs) to motor multi-turn target.
  float outputToMotorMT(float target_output_abs_rad, float current_motor_mt_rad);

} // namespace GripperAPI
