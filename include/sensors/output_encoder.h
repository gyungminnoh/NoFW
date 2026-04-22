#pragma once

#include <stdint.h>

#include "config/actuator_types.h"

enum class OutputEncoderStatus : uint8_t {
  Ok = 0,
  NotReady = 1,
  Invalid = 2,
  CalibrationRequired = 3,
  Fault = 4,
};

struct OutputAngleSample {
  float angle_rad = 0.0f;
  OutputEncoderStatus status = OutputEncoderStatus::NotReady;
};

class IOutputEncoder {
 public:
  virtual ~IOutputEncoder() = default;

  virtual bool begin() = 0;
  virtual OutputEncoderType type() const = 0;
  virtual bool isCalibrated() const = 0;
  virtual bool read(OutputAngleSample& out) = 0;
  virtual bool readAbsoluteAngleRad(float& out_angle_rad) = 0;
};
