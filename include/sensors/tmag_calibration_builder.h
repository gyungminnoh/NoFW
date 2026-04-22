#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config/calibration_data.h"

struct TmagCalibrationSample {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  float input_angle_rad = 0.0f;
  float reference_angle_rad = 0.0f;
};

struct TmagCalibrationMetrics {
  size_t sample_count = 0;
  uint16_t valid_bin_count = 0;
  float calibration_rms_rad = 0.0f;
  float validation_rms_rad = 0.0f;
  float validation_mae_rad = 0.0f;
  float validation_max_abs_rad = 0.0f;
};

class TmagCalibrationBuilder {
 public:
  static constexpr size_t kMaxSamples = 3072;

  void reset(float learned_gear_ratio);
  bool addSample(const TmagCalibrationSample& sample);
  size_t sampleCount() const;

  bool build(TmagCalibrationData& out_calibration,
             TmagCalibrationMetrics* out_metrics = nullptr) const;

  static bool estimateAngleRad(const TmagCalibrationData& calibration,
                               int16_t x_raw,
                               int16_t y_raw,
                               int16_t z_raw,
                               float input_single_turn_rad,
                               float& out_angle_rad,
                               int* out_best_bin = nullptr);

 private:
  TmagCalibrationSample samples_[kMaxSamples] = {};
  size_t sample_count_ = 0;
  float learned_gear_ratio_ = 1.0f;
};
