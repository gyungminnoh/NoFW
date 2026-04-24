#pragma once
#include <Arduino.h>

// =================== Pins (confirmed) ===================
// DRV8302 strap / control
static constexpr uint8_t PIN_EN_GATE  = PB7;
static constexpr uint8_t PIN_M_PWM    = PB4;
static constexpr uint8_t PIN_M_OC     = PB5;
static constexpr uint8_t PIN_OC_GAIN  = PB6;

// 3PWM outputs
static constexpr uint8_t PIN_PWM_A = PA10;   // Phase A
static constexpr uint8_t PIN_PWM_B = PA9;    // Phase B
static constexpr uint8_t PIN_PWM_C = PA8;    // Phase C

// AS5048A SPI
static constexpr uint8_t PIN_AS5048_CS = PB10;
static constexpr uint8_t PIN_FRAM_CS   = PB12;
static constexpr uint8_t PIN_SPI1_NCS_3 = PA4;
static constexpr uint8_t PIN_TMAG5170_CS = PIN_SPI1_NCS_3;

// =================== AS5600 (I2C) bootstrap only ===================
static constexpr uint8_t AS5600_I2C_ADDR = 0x36;
static constexpr uint8_t PIN_I2C_SCL = PB8;
static constexpr uint8_t PIN_I2C_SDA = PB9;

// =================== AS5048A ===================
static constexpr bool AS5048A_INVERT = true; // Invert direction to match motor wiring

// =================== Motor / power ===================
static constexpr int   POLE_PAIRS     = 14;
static constexpr float BUS_VOLTAGE    = 40.0f;
static constexpr float VOLTAGE_LIMIT  = 40.0f;
static constexpr float ALIGN_VOLTAGE  = 1.0f;

// =================== Gear ratio ===================
// motor_angle_rad = output_angle_rad * GEAR_RATIO
static constexpr float GEAR_RATIO = 8.0f;

// =================== Actuator travel ===================
// Motor shaft max travel, in turns.
static constexpr float ACTUATOR_MOTOR_TURNS = 48.0f;
// Motor travel range in radians.
static constexpr float ACTUATOR_MOTOR_MAX_RAD = ACTUATOR_MOTOR_TURNS * 2.0f * 3.1415926f;
// Output angle range in radians (output shaft reference).
static constexpr float ACTUATOR_OUTPUT_MAX_RAD = ACTUATOR_MOTOR_MAX_RAD / GEAR_RATIO;
// Output angle range in degrees (output shaft reference).
static constexpr float ACTUATOR_OUTPUT_MAX_DEG =
    ACTUATOR_OUTPUT_MAX_RAD * (180.0f / 3.1415926f);
// =================== CAN (CONFIRMED) ===================
static constexpr uint32_t CAN_BITRATE    = 1000000; // 1 Mbps
static constexpr uint32_t CAN_TIMEOUT_MS = 100;
static constexpr uint32_t CAN_STATUS_TX_MS  = 50; // Status report period (0=disable)
static constexpr uint8_t  CAN_NODE_ID    = 7;

// Actuator CAN ID base (11-bit standard)
static constexpr uint16_t CAN_ID_OUTPUT_ANGLE_CMD_BASE = 0x200;
static constexpr uint16_t CAN_ID_OUTPUT_ANGLE_STATUS_BASE = 0x400;
static constexpr uint16_t CAN_ID_OUTPUT_VEL_CMD_BASE = 0x210;
static constexpr uint16_t CAN_ID_OUTPUT_VEL_STATUS_BASE = 0x410;
static constexpr uint16_t CAN_ID_ACTUATOR_LIMITS_STATUS_BASE = 0x420;
static constexpr uint16_t CAN_ID_ACTUATOR_CONFIG_STATUS_BASE = 0x430;
static constexpr uint16_t CAN_ID_OUTPUT_PROFILE_CMD_BASE = 0x220;
static constexpr uint16_t CAN_ID_POWER_STAGE_CMD_BASE = 0x230;
static constexpr uint16_t CAN_ID_ACTUATOR_LIMITS_CONFIG_CMD_BASE = 0x240;
static constexpr uint16_t CAN_ID_ACTUATOR_GEAR_CONFIG_CMD_BASE = 0x250;
static constexpr uint16_t CAN_ID_OUTPUT_ENCODER_CONFIG_CMD_BASE = 0x260;
static constexpr uint16_t CAN_ID_OUTPUT_ENCODER_AUTO_CAL_CMD_BASE = 0x270;

// CAN uses the board's default CAN1 pin mapping provided by the STM32 core.
// Transceiver: SN65HVD230 (no SW control unless STB/RS wired to MCU)

// =================== User button ===================
static constexpr uint8_t PIN_USER_BTN = PC13;

// =================== Torque limit ===================
// SimpleFOC torque limit via voltage (tune to prevent overheating).
// Set lower than VOLTAGE_LIMIT to reduce torque.
static constexpr float TORQUE_LIMIT_VOLTS = 40.0f;

// =================== Motion limits ===================
// These limits are defined on the motor shaft side. Output-side limits are
// derived from gear_ratio at runtime.
static constexpr float ACTUATOR_MOTOR_VELOCITY_LIMIT_RAD_S = 320.0f;
static constexpr float ACTUATOR_MOTOR_ACCEL_LIMIT_RAD_S2 = 2000.0f;

// Conservative output-side slew limit for CAN velocity commands. This is kept
// separate from the outer position-loop accel limit so that a large velocity
// step does not immediately become a large motor-side demand.
static constexpr float ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 180.0f;

// Angle-mode edge braking uses a separate, much softer output-side deceleration
// target than the general outer-loop accel limit. This is intended to reduce
// aggressive regenerative braking near configured travel limits.
static constexpr float ACTUATOR_OUTPUT_EDGE_BRAKE_DEG_S2 = 60.0f;

// Final slew limiter for the angle-mode velocity command after all outer-loop
// shaping and edge-braking caps. This specifically limits abrupt command
// reversals or deceleration spikes that can otherwise create strong
// regenerative braking near the travel edges.
static constexpr float ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 300.0f;
