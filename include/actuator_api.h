#pragma once

#include <Arduino.h>

#include "config/actuator_config.h"

namespace ActuatorAPI {

void configure(const ActuatorConfig& actuator_config);

extern volatile float target_output_deg;
extern volatile float target_output_velocity_deg_s;
extern volatile ControlMode active_control_mode;

extern float motor_zero_mt_rad;
extern float output_raw_boot_rad;
extern float output_zero_ref_rad;

void setBootReference(float motor_mt_now_rad, float output_boot_rad);
void setBootReferenceFromMotor(float motor_mt_now_rad);

float getTargetOutputAbsRad();
float getTargetOutputDeg();

float motorMTToOutputAbsRad(float motor_mt_rad);
float motorMTToOutputRawRad(float motor_mt_rad);
float motorMTToOutputAbsDeg(float motor_mt_rad);
float motorMTToOutputRawDeg(float motor_mt_rad);

float outputDegToMotorMT(float target_output_deg, float current_motor_mt_rad);
float outputVelocityDegPerSecToMotorVelocity(float velocity_deg_s);
float motorVelocityToOutputVelocityDegPerSec(float motor_velocity_rad_s);

}  // namespace ActuatorAPI

