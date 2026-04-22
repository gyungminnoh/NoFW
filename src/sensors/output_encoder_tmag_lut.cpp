#include "sensors/output_encoder_tmag_lut.h"

namespace {

float wrap0To2Pi(float angle_rad) {
  while (angle_rad >= 2.0f * PI) angle_rad -= 2.0f * PI;
  while (angle_rad < 0.0f) angle_rad += 2.0f * PI;
  return angle_rad;
}

}  // namespace

bool OutputEncoderTmagLut::begin() {
  estimator_.setCalibration(calibration_);
  return estimator_.begin();
}

OutputEncoderType OutputEncoderTmagLut::type() const {
  return OutputEncoderType::TmagLut;
}

bool OutputEncoderTmagLut::isCalibrated() const {
  return calibration_.valid;
}

bool OutputEncoderTmagLut::read(OutputAngleSample& out) {
  out = {};
  if (!calibration_.valid) {
    out.status = OutputEncoderStatus::CalibrationRequired;
    return false;
  }

  float angle_rad = 0.0f;
  if (!readAbsoluteAngleRad(angle_rad)) {
    out.status = OutputEncoderStatus::Invalid;
    return false;
  }
  out.angle_rad = wrap0To2Pi(angle_rad - calibration_.zero_offset_rad);
  out.status = OutputEncoderStatus::Ok;
  return true;
}

bool OutputEncoderTmagLut::readAbsoluteAngleRad(float& out_angle_rad) {
  return estimator_.readAngleRad(out_angle_rad);
}

void OutputEncoderTmagLut::setInputSensor(Sensor* input_sensor) {
  estimator_.attachInputSensor(input_sensor);
}

void OutputEncoderTmagLut::setCalibration(const TmagCalibrationData& calibration) {
  calibration_ = calibration;
  estimator_.setCalibration(calibration_);
}

const TmagCalibrationData& OutputEncoderTmagLut::calibration() const {
  return calibration_;
}
