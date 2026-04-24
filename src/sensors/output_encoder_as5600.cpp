#include "sensors/output_encoder_as5600.h"

#include <math.h>

#include "as5600_bootstrap.h"

namespace {

constexpr float kTwoPi = 2.0f * PI;

float wrap0To2Pi(float angle_rad) {
  while (angle_rad >= kTwoPi) angle_rad -= kTwoPi;
  while (angle_rad < 0.0f) angle_rad += kTwoPi;
  return angle_rad;
}

float wrapToPi(float angle_rad) {
  while (angle_rad > PI) angle_rad -= kTwoPi;
  while (angle_rad < -PI) angle_rad += kTwoPi;
  return angle_rad;
}

}  // namespace

bool OutputEncoderAs5600::begin() {
  float angle_rad = 0.0f;
  ready_ = readAs5600AngleRad(angle_rad);
  return ready_;
}

OutputEncoderType OutputEncoderAs5600::type() const {
  return OutputEncoderType::As5600;
}

bool OutputEncoderAs5600::isCalibrated() const {
  return calibration_.valid;
}

bool OutputEncoderAs5600::read(OutputAngleSample& out) {
  out = {};

  if (!ready_) {
    out.status = OutputEncoderStatus::NotReady;
    return false;
  }

  if (!calibration_.valid) {
    out.status = OutputEncoderStatus::CalibrationRequired;
    return false;
  }

  float angle_rad = 0.0f;
  if (!readZeroRelativeAngleRad(angle_rad)) {
    out.status = OutputEncoderStatus::Invalid;
    return false;
  }

  out.angle_rad = wrap0To2Pi(angle_rad);
  out.status = OutputEncoderStatus::Ok;
  return true;
}

bool OutputEncoderAs5600::readAbsoluteAngleRad(float& out_angle_rad) {
  if (!ready_) {
    return false;
  }
  return readAs5600AngleRad(out_angle_rad);
}

bool OutputEncoderAs5600::readZeroRelativeAngleRad(float& out_angle_rad) {
  if (!ready_ || !calibration_.valid) {
    return false;
  }
  float angle_rad = 0.0f;
  if (!readAs5600AngleRad(angle_rad)) {
    return false;
  }

  float delta_rad = wrapToPi(angle_rad - calibration_.zero_offset_rad);
  if (calibration_.invert) {
    delta_rad = -delta_rad;
  }
  out_angle_rad = wrapToPi(delta_rad);
  return true;
}

void OutputEncoderAs5600::setCalibration(const As5600CalibrationData& calibration) {
  calibration_ = calibration;
}

const As5600CalibrationData& OutputEncoderAs5600::calibration() const {
  return calibration_;
}
