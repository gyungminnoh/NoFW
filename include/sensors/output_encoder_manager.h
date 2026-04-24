#pragma once

#include <SimpleFOC.h>

#include "config/actuator_config.h"
#include "config/calibration_data.h"
#include "sensors/output_encoder.h"
#include "sensors/output_encoder_as5600.h"
#include "sensors/output_encoder_direct_input.h"
#include "sensors/output_encoder_tmag_lut.h"

class OutputEncoderManager {
 public:
  bool configure(const ActuatorConfig& actuator_config,
                 const DirectInputCalibrationData* direct_input_calibration,
                 const As5600CalibrationData* as5600_calibration,
                 const TmagCalibrationData* tmag_calibration,
                 Sensor* input_sensor);

  IOutputEncoder* active();
  const IOutputEncoder* active() const;
  bool readAbsoluteAngleRad(float& out_angle_rad) const;
  bool readZeroRelativeAngleRad(float& out_angle_rad) const;
  void updateZeroOffset(float zero_rad, uint32_t magic);
  const DirectInputCalibrationData& directInputCalibration() const;
  const As5600CalibrationData& as5600Calibration() const;
  const TmagCalibrationData& tmagCalibration() const;

 private:
  ActuatorConfig actuator_config_ = {};
  OutputEncoderDirectInput output_encoder_direct_input_ = {};
  OutputEncoderAs5600 output_encoder_as5600_ = {};
  OutputEncoderTmagLut output_encoder_tmag_lut_ = {};
  IOutputEncoder* active_encoder_ = nullptr;
};
