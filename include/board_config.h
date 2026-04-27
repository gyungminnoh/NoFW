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
#ifndef BUILD_POLE_PAIRS
#define BUILD_POLE_PAIRS 14
#endif
static constexpr int   POLE_PAIRS     = BUILD_POLE_PAIRS;
static constexpr float BUS_VOLTAGE    = 40.0f;
static constexpr float VOLTAGE_LIMIT  = 40.0f;
#ifndef BUILD_ALIGN_VOLTAGE
#define BUILD_ALIGN_VOLTAGE 1.0f
#endif
static constexpr float ALIGN_VOLTAGE  = BUILD_ALIGN_VOLTAGE;

// =================== Gear ratio ===================
// motor_angle_rad = output_angle_rad * GEAR_RATIO
#ifndef BUILD_GEAR_RATIO
#define BUILD_GEAR_RATIO 50.0f
#endif
static constexpr float GEAR_RATIO = BUILD_GEAR_RATIO;

// =================== Motor/output direction ===================
#ifndef BUILD_MOTOR_TO_OUTPUT_SIGN
#define BUILD_MOTOR_TO_OUTPUT_SIGN 1
#endif
static_assert(BUILD_MOTOR_TO_OUTPUT_SIGN == 1 || BUILD_MOTOR_TO_OUTPUT_SIGN == -1,
              "BUILD_MOTOR_TO_OUTPUT_SIGN must be 1 or -1");
static constexpr int8_t MOTOR_TO_OUTPUT_SIGN =
    static_cast<int8_t>(BUILD_MOTOR_TO_OUTPUT_SIGN);

// =================== Actuator travel ===================
// Default deployed travel limits for this board build.
static constexpr float ACTUATOR_OUTPUT_DEFAULT_MIN_DEG = -120.0f;
static constexpr float ACTUATOR_OUTPUT_DEFAULT_MAX_DEG = 120.0f;
// =================== CAN (CONFIRMED) ===================
static constexpr uint32_t CAN_BITRATE    = 1000000; // 1 Mbps
static constexpr uint32_t CAN_TIMEOUT_MS = 100;
static constexpr uint32_t CAN_STATUS_TX_MS  = 50; // Status report period (0=disable)
#ifndef BUILD_CAN_NODE_ID
#error "BUILD_CAN_NODE_ID must be defined by the selected PlatformIO environment"
#endif
static_assert(BUILD_CAN_NODE_ID >= 0 && BUILD_CAN_NODE_ID <= 127,
              "BUILD_CAN_NODE_ID must fit in 11-bit standard CAN node range");
// Single source of truth: select the PlatformIO environment for the target board.
static constexpr uint8_t  CAN_NODE_ID    = static_cast<uint8_t>(BUILD_CAN_NODE_ID);

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
static constexpr uint16_t CAN_ID_OUTPUT_ENCODER_ZERO_CMD_BASE = 0x280;
static constexpr uint16_t CAN_ID_FOC_CALIBRATION_CMD_BASE = 0x290;
static constexpr uint16_t CAN_ID_ACTUATOR_VOLTAGE_LIMIT_CONFIG_CMD_BASE = 0x2A0;
static constexpr uint16_t CAN_ID_ACTUATOR_VOLTAGE_LIMIT_STATUS_BASE = 0x440;

// CAN uses the board's default CAN1 pin mapping provided by the STM32 core.
// Transceiver: SN65HVD230 (no SW control unless STB/RS wired to MCU)

// =================== User button ===================
static constexpr uint8_t PIN_USER_BTN = PC13;

// =================== Torque limit ===================
// SimpleFOC torque limit via voltage (tune to prevent overheating).
// Set lower than VOLTAGE_LIMIT to reduce torque.
#ifndef BUILD_TORQUE_LIMIT_VOLTS
#define BUILD_TORQUE_LIMIT_VOLTS 12.0f
#endif
static constexpr float TORQUE_LIMIT_VOLTS = BUILD_TORQUE_LIMIT_VOLTS;

// =================== Control gains ===================
#ifndef BUILD_MOTOR_VELOCITY_PID_P
#define BUILD_MOTOR_VELOCITY_PID_P 0.12f
#endif
#ifndef BUILD_MOTOR_VELOCITY_PID_I
#define BUILD_MOTOR_VELOCITY_PID_I 2.0f
#endif
#ifndef BUILD_MOTOR_VELOCITY_PID_D
#define BUILD_MOTOR_VELOCITY_PID_D 0.0f
#endif
#ifndef BUILD_MOTOR_VELOCITY_LPF_TF
#define BUILD_MOTOR_VELOCITY_LPF_TF 0.007f
#endif
#ifndef BUILD_OUTER_ANGLE_KP
#define BUILD_OUTER_ANGLE_KP 3.0f
#endif

static constexpr float MOTOR_VELOCITY_PID_P = BUILD_MOTOR_VELOCITY_PID_P;
static constexpr float MOTOR_VELOCITY_PID_I = BUILD_MOTOR_VELOCITY_PID_I;
static constexpr float MOTOR_VELOCITY_PID_D = BUILD_MOTOR_VELOCITY_PID_D;
static constexpr float MOTOR_VELOCITY_LPF_TF = BUILD_MOTOR_VELOCITY_LPF_TF;
static constexpr float OUTER_ANGLE_KP = BUILD_OUTER_ANGLE_KP;

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

// Following-error protection. If the actuator is commanded hard but the output
// angle does not move enough for this long, the power stage is fault-disarmed.
static constexpr float ACTUATOR_FOLLOWING_ERROR_FAULT_DEG = 30.0f;
static constexpr float ACTUATOR_FOLLOWING_ERROR_LOW_VEL_DEG_S = 5.0f;
static constexpr float ACTUATOR_FOLLOWING_ERROR_MIN_CMD_DEG_S = 10.0f;
static constexpr uint32_t ACTUATOR_FOLLOWING_ERROR_FAULT_MS = 1500;
