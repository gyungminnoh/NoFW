#pragma once

#include "config/actuator_config.h"

ActuatorConfig buildDefaultActuatorConfig();
void applyOutputProfileDefaults(ActuatorConfig& config, OutputEncoderType profile);
bool isDirectInputCompatible(const ActuatorConfig& config);
bool syncActuatorConfigToFirmwareDefaults(ActuatorConfig& config);
