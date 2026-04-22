#pragma once

#include <stdint.h>

constexpr uint16_t kTmagLutBins = 256;

struct TmagLutBinData {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
};

struct FocCalibrationData {
  uint32_t magic = 0;
  int8_t sensor_direction = 1;
  float zero_electrical_angle = 0.0f;
  bool valid = false;
};

struct As5600CalibrationData {
  uint32_t magic = 0;
  float zero_offset_rad = 0.0f;
  bool invert = false;
  bool valid = false;
};

struct DirectInputCalibrationData {
  uint32_t magic = 0;
  float zero_offset_rad = 0.0f;
  bool valid = false;
};

struct TmagCalibrationData {
  uint32_t magic = 0;

  float zero_offset_rad = 0.0f;
  float learned_gear_ratio = 1.0f;
  float input_phase_rad = 0.0f;
  float amp_x = 1.0f;
  float amp_y = 1.0f;
  float amp_z = 1.0f;

  int8_t input_sign = 1;
  uint16_t lut_bin_count = 0;
  uint16_t valid_bin_count = 0;
  bool valid = false;

  float calibration_rms_rad = 0.0f;
  float validation_rms_rad = 0.0f;

  TmagLutBinData lut[kTmagLutBins] = {};
};
