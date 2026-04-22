#pragma once

#include <SimpleFOC.h>

#include "config/calibration_data.h"

class TmagLutEstimator {
 public:
  bool begin();
  void setCalibration(const TmagCalibrationData& calibration);
  void attachInputSensor(Sensor* input_sensor);
  bool isConfigured() const;
  bool readAngleRad(float& out_angle_rad);

 private:
  bool initTmag();
  bool readTmagXYZ(int16_t& x_raw, int16_t& y_raw, int16_t& z_raw) const;

  TmagCalibrationData calibration_ = {};
  Sensor* input_sensor_ = nullptr;
  bool tmag_ready_ = false;
};
