#pragma once

#include "config/actuator_config.h"
#include "config/calibration_data.h"

namespace ConfigStore {

struct CalibrationBundle {
  FocCalibrationData foc = {};
  DirectInputCalibrationData direct_input = {};
  As5600CalibrationData as5600 = {};
  TmagCalibrationData tmag = {};
};

enum class CalibrationLoadStatus : uint8_t {
  None = 0,
  Trusted = 1,
};

bool loadActuatorConfig(ActuatorConfig& out_config);
bool saveActuatorConfig(const ActuatorConfig& config);

bool loadCalibrationBundle(CalibrationBundle& out_bundle);
bool loadCalibrationBundleWithStatus(CalibrationBundle& out_bundle,
                                     CalibrationLoadStatus& out_status);
bool saveCalibrationBundle(const CalibrationBundle& bundle);
bool clearCalibrationBundle();

}  // namespace ConfigStore
