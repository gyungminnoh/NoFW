#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SimpleFOC.h>

// Board configuration
#include "board_config.h"

// Modules
#include "as5048a_custom_sensor.h"
#include "actuator_api.h"
#include "multi_turn_estimator.h"
#include "position_velocity_controller.h"
#include "as5600_bootstrap.h"
#include "fm25cl64b_fram.h"
#include "fram_self_test.h"
#include "can_service.h"
#include "config/actuator_config.h"
#include "config/calibration_data.h"
#include "sensors/output_encoder.h"
#include "sensors/output_encoder_manager.h"
#include "storage/config_store.h"
