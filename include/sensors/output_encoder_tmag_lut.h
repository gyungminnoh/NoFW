#pragma once

#include <SimpleFOC.h>

#include "config/calibration_data.h"
#include "sensors/output_encoder.h"
#include "sensors/tmag_lut_estimator.h"

class OutputEncoderTmagLut : public IOutputEncoder {
 public:
  OutputEncoderTmagLut() = default;

  bool begin() override;
  OutputEncoderType type() const override;
  bool isCalibrated() const override;
  bool read(OutputAngleSample& out) override;
  bool readAbsoluteAngleRad(float& out_angle_rad) override;
  bool readZeroRelativeAngleRad(float& out_angle_rad) override;

  void setInputSensor(Sensor* input_sensor);
  void setCalibration(const TmagCalibrationData& calibration);
  const TmagCalibrationData& calibration() const;

 private:
  TmagCalibrationData calibration_ = {};
  TmagLutEstimator estimator_ = {};
};
