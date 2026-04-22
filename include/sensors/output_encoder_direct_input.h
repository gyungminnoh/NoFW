#pragma once

#include <SimpleFOC.h>

#include "config/calibration_data.h"
#include "sensors/output_encoder.h"

class OutputEncoderDirectInput : public IOutputEncoder {
 public:
  OutputEncoderDirectInput() = default;

  bool begin() override;
  OutputEncoderType type() const override;
  bool isCalibrated() const override;
  bool read(OutputAngleSample& out) override;
  bool readAbsoluteAngleRad(float& out_angle_rad) override;

  void setInputSensor(Sensor* input_sensor);
  void setCalibration(const DirectInputCalibrationData& calibration);
  const DirectInputCalibrationData& calibration() const;

 private:
  Sensor* input_sensor_ = nullptr;
  DirectInputCalibrationData calibration_ = {};
};

