#include "sensors/output_encoder_direct_input.h"

namespace {

float wrap0To2Pi(float angle_rad) {
  while (angle_rad >= 2.0f * PI) angle_rad -= 2.0f * PI;
  while (angle_rad < 0.0f) angle_rad += 2.0f * PI;
  return angle_rad;
}

float wrapToPi(float angle_rad) {
  while (angle_rad > PI) angle_rad -= 2.0f * PI;
  while (angle_rad < -PI) angle_rad += 2.0f * PI;
  return angle_rad;
}

}  // namespace

bool OutputEncoderDirectInput::begin() {
  return input_sensor_ != nullptr;
}

OutputEncoderType OutputEncoderDirectInput::type() const {
  return OutputEncoderType::DirectInput;
}

bool OutputEncoderDirectInput::isCalibrated() const {
  return calibration_.valid;
}

bool OutputEncoderDirectInput::read(OutputAngleSample& out) {
  out = {};
  if (!calibration_.valid) {
    out.status = OutputEncoderStatus::CalibrationRequired;
    return false;
  }

  float angle_rad = 0.0f;
  if (!readZeroRelativeAngleRad(angle_rad)) {
    out.status = OutputEncoderStatus::NotReady;
    return false;
  }
  out.angle_rad = wrap0To2Pi(angle_rad);
  out.status = OutputEncoderStatus::Ok;
  return true;
}

bool OutputEncoderDirectInput::readAbsoluteAngleRad(float& out_angle_rad) {
  if (input_sensor_ == nullptr) {
    return false;
  }
  input_sensor_->update();
  out_angle_rad = input_sensor_->getAngle();
  return true;
}

bool OutputEncoderDirectInput::readZeroRelativeAngleRad(float& out_angle_rad) {
  if (!calibration_.valid) {
    return false;
  }
  float angle_rad = 0.0f;
  if (!readAbsoluteAngleRad(angle_rad)) {
    return false;
  }
  out_angle_rad = wrapToPi(angle_rad - calibration_.zero_offset_rad);
  return true;
}

void OutputEncoderDirectInput::setInputSensor(Sensor* input_sensor) {
  input_sensor_ = input_sensor;
}

void OutputEncoderDirectInput::setCalibration(const DirectInputCalibrationData& calibration) {
  calibration_ = calibration;
}

const DirectInputCalibrationData& OutputEncoderDirectInput::calibration() const {
  return calibration_;
}
