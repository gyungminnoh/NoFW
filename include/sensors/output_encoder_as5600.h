#pragma once

#include "config/calibration_data.h"
#include "sensors/output_encoder.h"

class OutputEncoderAs5600 : public IOutputEncoder {
 public:
  OutputEncoderAs5600() = default;

  bool begin() override;
  OutputEncoderType type() const override;
  bool isCalibrated() const override;
  bool read(OutputAngleSample& out) override;
  bool readAbsoluteAngleRad(float& out_angle_rad) override;
  bool readZeroRelativeAngleRad(float& out_angle_rad) override;

  void setCalibration(const As5600CalibrationData& calibration);
  const As5600CalibrationData& calibration() const;

 private:
  As5600CalibrationData calibration_ = {};
  bool ready_ = false;
};
