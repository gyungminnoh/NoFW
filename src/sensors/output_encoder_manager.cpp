#include "sensors/output_encoder_manager.h"

namespace {

bool hasValidDirectInputCalibration(const DirectInputCalibrationData* calibration) {
  return calibration != nullptr && calibration->valid;
}

bool hasValidAs5600Calibration(const As5600CalibrationData* calibration) {
  return calibration != nullptr && calibration->valid;
}

bool hasValidTmagCalibration(const TmagCalibrationData* calibration) {
  return calibration != nullptr && calibration->valid;
}

}  // namespace

bool OutputEncoderManager::configure(const ActuatorConfig& actuator_config,
                                     const DirectInputCalibrationData* direct_input_calibration,
                                     const As5600CalibrationData* as5600_calibration,
                                     const TmagCalibrationData* tmag_calibration,
                                     Sensor* input_sensor) {
  actuator_config_ = actuator_config;
  active_encoder_ = nullptr;

  OutputEncoderType selected_type = actuator_config_.output_encoder_type;
  if (selected_type == OutputEncoderType::TmagLut &&
      (!hasValidTmagCalibration(tmag_calibration) || input_sensor == nullptr)) {
    selected_type = OutputEncoderType::As5600;
  }

  actuator_config_.output_encoder_type = selected_type;

  switch (selected_type) {
    case OutputEncoderType::VelocityOnly:
      active_encoder_ = nullptr;
      break;

    case OutputEncoderType::DirectInput:
      if (hasValidDirectInputCalibration(direct_input_calibration)) {
        output_encoder_direct_input_.setCalibration(*direct_input_calibration);
      } else {
        output_encoder_direct_input_.setCalibration(DirectInputCalibrationData{});
      }
      output_encoder_direct_input_.setInputSensor(input_sensor);
      active_encoder_ = &output_encoder_direct_input_;
      break;

    case OutputEncoderType::As5600:
      if (hasValidAs5600Calibration(as5600_calibration)) {
        output_encoder_as5600_.setCalibration(*as5600_calibration);
      } else {
        output_encoder_as5600_.setCalibration(As5600CalibrationData{});
      }
      active_encoder_ = &output_encoder_as5600_;
      break;

    case OutputEncoderType::TmagLut:
      if (hasValidTmagCalibration(tmag_calibration)) {
        output_encoder_tmag_lut_.setCalibration(*tmag_calibration);
      } else {
        output_encoder_tmag_lut_.setCalibration(TmagCalibrationData{});
      }
      output_encoder_tmag_lut_.setInputSensor(input_sensor);
      active_encoder_ = &output_encoder_tmag_lut_;
      break;
  }

  if (selected_type == OutputEncoderType::VelocityOnly) {
    return true;
  }

  if (active_encoder_ == nullptr) {
    return false;
  }
  active_encoder_->begin();
  return true;
}

IOutputEncoder* OutputEncoderManager::active() {
  return active_encoder_;
}

const IOutputEncoder* OutputEncoderManager::active() const {
  return active_encoder_;
}

bool OutputEncoderManager::readAbsoluteAngleRad(float& out_angle_rad) const {
  if (active_encoder_ == nullptr) {
    return false;
  }
  return active_encoder_->readAbsoluteAngleRad(out_angle_rad);
}

void OutputEncoderManager::updateZeroOffset(float zero_rad, uint32_t magic) {
  if (active_encoder_ == nullptr) {
    return;
  }

  switch (active_encoder_->type()) {
    case OutputEncoderType::VelocityOnly:
      break;

    case OutputEncoderType::DirectInput: {
      DirectInputCalibrationData calibration = output_encoder_direct_input_.calibration();
      calibration.magic = magic;
      calibration.zero_offset_rad = zero_rad;
      calibration.valid = true;
      output_encoder_direct_input_.setCalibration(calibration);
      break;
    }

    case OutputEncoderType::As5600: {
      As5600CalibrationData calibration = output_encoder_as5600_.calibration();
      calibration.magic = magic;
      calibration.zero_offset_rad = zero_rad;
      calibration.invert = false;
      calibration.valid = true;
      output_encoder_as5600_.setCalibration(calibration);
      break;
    }

    case OutputEncoderType::TmagLut: {
      TmagCalibrationData calibration = output_encoder_tmag_lut_.calibration();
      calibration.magic = magic;
      calibration.zero_offset_rad = zero_rad;
      output_encoder_tmag_lut_.setCalibration(calibration);
      break;
    }
  }
}

const DirectInputCalibrationData& OutputEncoderManager::directInputCalibration() const {
  return output_encoder_direct_input_.calibration();
}

const As5600CalibrationData& OutputEncoderManager::as5600Calibration() const {
  return output_encoder_as5600_.calibration();
}

const TmagCalibrationData& OutputEncoderManager::tmagCalibration() const {
  return output_encoder_tmag_lut_.calibration();
}
