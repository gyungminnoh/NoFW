#include "sensors/tmag_lut_estimator.h"

#include <math.h>

#include "sensors/tmag_calibration_builder.h"
#include "tmag5170_spi.h"

namespace {

constexpr uint8_t kRegDeviceConfig = 0x00;
constexpr uint8_t kRegSensorConfig = 0x01;
constexpr uint8_t kRegSystemConfig = 0x02;
constexpr uint8_t kRegXResult = 0x09;
constexpr uint8_t kRegYResult = 0x0A;
constexpr uint8_t kRegZResult = 0x0B;

constexpr uint16_t kDeviceConfigActive32x = 0x5020;
constexpr uint16_t kSensorConfigXYZ = 0x01C0;
constexpr uint16_t kSystemConfigDefault = 0x0000;
float wrap0To2Pi(float angle_rad) {
  while (angle_rad >= 2.0f * PI) angle_rad -= 2.0f * PI;
  while (angle_rad < 0.0f) angle_rad += 2.0f * PI;
  return angle_rad;
}

}  // namespace

bool TmagLutEstimator::begin() {
  tmag_ready_ = initTmag();
  return tmag_ready_;
}

void TmagLutEstimator::setCalibration(const TmagCalibrationData& calibration) {
  calibration_ = calibration;
}

void TmagLutEstimator::attachInputSensor(Sensor* input_sensor) {
  input_sensor_ = input_sensor;
}

bool TmagLutEstimator::isConfigured() const {
  return tmag_ready_ && input_sensor_ != nullptr && calibration_.valid &&
         calibration_.lut_bin_count > 0 && calibration_.lut_bin_count <= kTmagLutBins &&
         calibration_.amp_x > 0.0f && calibration_.amp_y > 0.0f && calibration_.amp_z > 0.0f &&
         fabsf(calibration_.learned_gear_ratio) >= 1.0f;
}

bool TmagLutEstimator::initTmag() {
  TMAG5170::begin();
  TMAG5170::disableCrc();
  TMAG5170::writeRegister(kRegDeviceConfig, 0x0000);
  TMAG5170::writeRegister(kRegSensorConfig, kSensorConfigXYZ);
  TMAG5170::writeRegister(kRegSystemConfig, kSystemConfigDefault);
  TMAG5170::writeRegister(kRegDeviceConfig, kDeviceConfigActive32x);
  return true;
}

bool TmagLutEstimator::readTmagXYZ(int16_t& x_raw, int16_t& y_raw, int16_t& z_raw) const {
  const auto x = TMAG5170::readRegister(kRegXResult);
  const auto y = TMAG5170::readRegister(kRegYResult);
  const auto z = TMAG5170::readRegister(kRegZResult);
  x_raw = static_cast<int16_t>(x.data);
  y_raw = static_cast<int16_t>(y.data);
  z_raw = static_cast<int16_t>(z.data);
  return true;
}

bool TmagLutEstimator::readAngleRad(float& out_angle_rad) {
  if (!isConfigured()) {
    return false;
  }

  input_sensor_->update();
  const float input_single_turn_rad = input_sensor_->getAngle();

  int16_t x_raw = 0;
  int16_t y_raw = 0;
  int16_t z_raw = 0;
  if (!readTmagXYZ(x_raw, y_raw, z_raw)) {
    return false;
  }

  float estimated = 0.0f;
  if (!TmagCalibrationBuilder::estimateAngleRad(
          calibration_, x_raw, y_raw, z_raw, input_single_turn_rad, estimated)) {
    return false;
  }
  out_angle_rad = wrap0To2Pi(estimated);
  return true;
}
