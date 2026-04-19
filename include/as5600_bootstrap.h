#pragma once
#include <Arduino.h>

// Reads AS5600 once (with stability check) in setup and returns angle [0, 2pi).
// Returns false on I2C error or unstable readings.
bool bootstrapOutputOffset(float& out_rad);

// Reads AS5600 angle [0, 2pi) with stability check.
// Returns false on I2C error or unstable readings.
bool readAs5600AngleRad(float& out_rad);
