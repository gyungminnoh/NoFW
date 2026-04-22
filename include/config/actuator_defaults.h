#pragma once

#include "config/actuator_config.h"

ActuatorConfig buildLegacyActuatorConfig();
void applyOutputProfileDefaults(ActuatorConfig& config, OutputEncoderType profile);
bool isDirectInputCompatible(const ActuatorConfig& config);
bool migrateStaleActuatorConfigDefaults(ActuatorConfig& config);
