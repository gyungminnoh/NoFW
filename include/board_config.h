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

// UART (USART2)
static constexpr uint8_t PIN_UART_TX = PA2;
static constexpr uint8_t PIN_UART_RX = PA3;

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

// =================== Gear ratio (CONFIRMED) ===================
// motor_angle_rad = output_angle_rad * GEAR_RATIO
static constexpr float GEAR_RATIO = 240.0f;

// =================== Gripper limits ===================
// Motor shaft max travel, in turns.
static constexpr float GRIP_MOTOR_TURNS = 48.0f;
// Motor travel range in radians.
static constexpr float GRIP_MOTOR_MAX_RAD = GRIP_MOTOR_TURNS * 2.0f * 3.1415926f;
// Output angle range in radians (output shaft reference).
static constexpr float GRIP_OUTPUT_MAX_RAD = GRIP_MOTOR_MAX_RAD / GEAR_RATIO;
// Boot target (% open). 0 = fully open, 100 = fully closed.
static constexpr float GRIP_BOOT_OPEN_PERCENT = 0.0f;
// Invert open/close direction (true = flip percent mapping).
static constexpr bool GRIP_INVERT_DIR = false;

// =================== CAN (CONFIRMED) ===================
static constexpr uint32_t CAN_BITRATE    = 1000000; // 1 Mbps
static constexpr uint32_t CAN_TIMEOUT_MS = 100;
static constexpr uint32_t CAN_POS_TX_MS  = 50; // Position report period (0=disable)
static constexpr uint8_t  CAN_NODE_ID    = 7;

// Gripper CAN ID base (11-bit standard)
static constexpr uint16_t CAN_ID_GRIP_CMD_BASE = 0x200;
static constexpr uint16_t CAN_ID_GRIP_POS_BASE = 0x400;

// STM32F446 CAN1 pins (CONFIRMED)
static constexpr uint8_t PIN_CAN_RX = PA11;
static constexpr uint8_t PIN_CAN_TX = PA12;

// Transceiver: SN65HVD230 (no SW control unless STB/RS wired to MCU)

// =================== User button ===================
static constexpr uint8_t PIN_USER_BTN = PC13;

// =================== Torque limit ===================
// SimpleFOC torque limit via voltage (tune to prevent overheating).
// Set lower than VOLTAGE_LIMIT to reduce torque.
static constexpr float TORQUE_LIMIT_VOLTS = 40.0f;
