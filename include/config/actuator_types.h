#pragma once

#include <stdint.h>

enum class OutputEncoderType : uint8_t {
  VelocityOnly = 0,
  As5600 = 1,
  TmagLut = 2,
  DirectInput = 3,
};

enum class ControlMode : uint8_t {
  OutputAngle = 1,
  OutputVelocity = 2,
};
