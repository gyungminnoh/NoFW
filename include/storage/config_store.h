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

bool loadActuatorConfig(ActuatorConfig& out_config);
bool saveActuatorConfig(const ActuatorConfig& config);

bool loadCalibrationBundle(CalibrationBundle& out_bundle);
bool saveCalibrationBundle(const CalibrationBundle& bundle);
bool clearCalibrationBundle();

bool loadCalibrationBundleCompat(CalibrationBundle& out_bundle);
bool saveCalibrationBundleCompat(const CalibrationBundle& bundle);
bool clearCalibrationBundleCompat();

}  // namespace ConfigStore
