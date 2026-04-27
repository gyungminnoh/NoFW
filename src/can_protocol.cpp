#include "can_protocol.h"
#include <math.h>
#include <stdint.h>

namespace CanProtocol {

uint16_t outputAngleCmdCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_ANGLE_CMD_BASE + node_id;
}

uint16_t outputAngleStatusCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_ANGLE_STATUS_BASE + node_id;
}

uint16_t outputVelocityCmdCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_VEL_CMD_BASE + node_id;
}

uint16_t outputVelocityStatusCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_VEL_STATUS_BASE + node_id;
}

uint16_t actuatorLimitsStatusCanId(uint8_t node_id) {
  return CAN_ID_ACTUATOR_LIMITS_STATUS_BASE + node_id;
}

uint16_t actuatorConfigStatusCanId(uint8_t node_id) {
  return CAN_ID_ACTUATOR_CONFIG_STATUS_BASE + node_id;
}

uint16_t outputProfileCmdCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_PROFILE_CMD_BASE + node_id;
}

uint16_t powerStageCmdCanId(uint8_t node_id) {
  return CAN_ID_POWER_STAGE_CMD_BASE + node_id;
}

uint16_t actuatorLimitsConfigCmdCanId(uint8_t node_id) {
  return CAN_ID_ACTUATOR_LIMITS_CONFIG_CMD_BASE + node_id;
}

uint16_t actuatorGearConfigCmdCanId(uint8_t node_id) {
  return CAN_ID_ACTUATOR_GEAR_CONFIG_CMD_BASE + node_id;
}

uint16_t outputEncoderConfigCmdCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_ENCODER_CONFIG_CMD_BASE + node_id;
}

uint16_t outputEncoderAutoCalCmdCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_ENCODER_AUTO_CAL_CMD_BASE + node_id;
}

uint16_t outputEncoderZeroCmdCanId(uint8_t node_id) {
  return CAN_ID_OUTPUT_ENCODER_ZERO_CMD_BASE + node_id;
}

uint16_t focCalibrationCmdCanId(uint8_t node_id) {
  return CAN_ID_FOC_CALIBRATION_CMD_BASE + node_id;
}

uint16_t actuatorVoltageLimitConfigCmdCanId(uint8_t node_id) {
  return CAN_ID_ACTUATOR_VOLTAGE_LIMIT_CONFIG_CMD_BASE + node_id;
}

uint16_t actuatorVoltageLimitStatusCanId(uint8_t node_id) {
  return CAN_ID_ACTUATOR_VOLTAGE_LIMIT_STATUS_BASE + node_id;
}

namespace {

bool decodeInt32MUnits_(const uint8_t data[8], uint8_t len, float& out_value) {
  if (len < 4) return false;
  const int32_t raw = static_cast<int32_t>(static_cast<uint32_t>(data[0]) |
                                           (static_cast<uint32_t>(data[1]) << 8) |
                                           (static_cast<uint32_t>(data[2]) << 16) |
                                           (static_cast<uint32_t>(data[3]) << 24));
  out_value = static_cast<float>(raw) * 0.001f;
  return true;
}

bool encodeInt32MUnits_(float value, uint8_t data[8], uint8_t& out_len) {
  const int32_t raw = static_cast<int32_t>(lroundf(value * 1000.0f));
  data[0] = static_cast<uint8_t>(raw & 0xFF);
  data[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
  data[2] = static_cast<uint8_t>((raw >> 16) & 0xFF);
  data[3] = static_cast<uint8_t>((raw >> 24) & 0xFF);
  out_len = 4;
  return true;
}

}  // namespace

bool decodeOutputAngleDeg_OptionA(const uint8_t data[8], uint8_t len, float& out_angle_deg) {
  return decodeInt32MUnits_(data, len, out_angle_deg);
}

bool encodeOutputAngleDeg_OptionA(float angle_deg, uint8_t data[8], uint8_t& out_len) {
  return encodeInt32MUnits_(angle_deg, data, out_len);
}

bool decodeOutputVelocityDegPerSec_OptionA(const uint8_t data[8],
                                           uint8_t len,
                                           float& out_velocity_deg_s) {
  return decodeInt32MUnits_(data, len, out_velocity_deg_s);
}

bool encodeOutputVelocityDegPerSec_OptionA(float velocity_deg_s,
                                           uint8_t data[8],
                                           uint8_t& out_len) {
  return encodeInt32MUnits_(velocity_deg_s, data, out_len);
}

bool encodeActuatorLimitsStatus_OptionA(float output_min_deg,
                                        float output_max_deg,
                                        uint8_t data[8],
                                        uint8_t& out_len) {
  uint8_t min_len = 0;
  if (!encodeInt32MUnits_(output_min_deg, data, min_len) || min_len != 4) {
    return false;
  }

  uint8_t max_data[8] = {0};
  uint8_t max_len = 0;
  if (!encodeInt32MUnits_(output_max_deg, max_data, max_len) || max_len != 4) {
    return false;
  }

  data[4] = max_data[0];
  data[5] = max_data[1];
  data[6] = max_data[2];
  data[7] = max_data[3];
  out_len = 8;
  return true;
}

bool encodeActuatorConfigStatus_OptionA(float gear_ratio,
                                        OutputEncoderType stored_profile,
                                        ControlMode default_control_mode,
                                        bool enable_velocity_mode,
                                        bool enable_output_angle_mode,
                                        uint8_t data[8],
                                        uint8_t& out_len) {
  uint8_t gear_len = 0;
  if (!encodeInt32MUnits_(gear_ratio, data, gear_len) || gear_len != 4) {
    return false;
  }

  data[4] = static_cast<uint8_t>(stored_profile);
  data[5] = static_cast<uint8_t>(default_control_mode);
  data[6] = 0;
  if (enable_velocity_mode) {
    data[6] |= 0x01;
  }
  if (enable_output_angle_mode) {
    data[6] |= 0x02;
  }
  data[7] = 0;
  out_len = 8;
  return true;
}

bool decodeActuatorLimitsConfigCmd_OptionA(const uint8_t data[8],
                                           uint8_t len,
                                           float& out_output_min_deg,
                                           float& out_output_max_deg) {
  if (len < 8) return false;
  if (!decodeInt32MUnits_(data, 4, out_output_min_deg)) {
    return false;
  }

  uint8_t max_data[8] = {0};
  max_data[0] = data[4];
  max_data[1] = data[5];
  max_data[2] = data[6];
  max_data[3] = data[7];
  return decodeInt32MUnits_(max_data, 4, out_output_max_deg);
}

bool encodeActuatorLimitsConfigCmd_OptionA(float output_min_deg,
                                           float output_max_deg,
                                           uint8_t data[8],
                                           uint8_t& out_len) {
  return encodeActuatorLimitsStatus_OptionA(output_min_deg, output_max_deg, data, out_len);
}

bool decodeActuatorGearConfigCmd_OptionA(const uint8_t data[8],
                                         uint8_t len,
                                         float& out_gear_ratio) {
  return decodeInt32MUnits_(data, len, out_gear_ratio);
}

bool encodeActuatorGearConfigCmd_OptionA(float gear_ratio,
                                         uint8_t data[8],
                                         uint8_t& out_len) {
  return encodeInt32MUnits_(gear_ratio, data, out_len);
}

bool decodeOutputProfileCmd_OptionA(const uint8_t data[8],
                                    uint8_t len,
                                    OutputEncoderType& out_profile) {
  if (len < 1) return false;
  switch (static_cast<OutputEncoderType>(data[0])) {
    case OutputEncoderType::VelocityOnly:
    case OutputEncoderType::As5600:
    case OutputEncoderType::TmagLut:
    case OutputEncoderType::DirectInput:
      out_profile = static_cast<OutputEncoderType>(data[0]);
      return true;
  }
  return false;
}

bool decodePowerStageCmd_OptionA(const uint8_t data[8],
                                 uint8_t len,
                                 bool& out_enable) {
  if (len < 1) return false;
  if (data[0] > 1) return false;
  out_enable = (data[0] != 0);
  return true;
}

bool decodeOutputEncoderConfigCmd_OptionA(const uint8_t data[8],
                                          uint8_t len,
                                          OutputEncoderType& out_encoder_type,
                                          bool& out_invert) {
  if (len < 2) return false;
  const auto encoder_type = static_cast<OutputEncoderType>(data[0]);
  if (encoder_type != OutputEncoderType::As5600) {
    return false;
  }
  if (data[1] > 1) {
    return false;
  }
  out_encoder_type = encoder_type;
  out_invert = (data[1] != 0);
  return true;
}

bool decodeOutputEncoderAutoCalCmd_OptionA(const uint8_t data[8],
                                           uint8_t len,
                                           OutputEncoderType& out_encoder_type) {
  if (len < 1) return false;
  const auto encoder_type = static_cast<OutputEncoderType>(data[0]);
  if (encoder_type != OutputEncoderType::As5600) {
    return false;
  }
  out_encoder_type = encoder_type;
  return true;
}

bool decodeOutputEncoderZeroCmd_OptionA(const uint8_t data[8],
                                        uint8_t len,
                                        OutputEncoderType& out_encoder_type) {
  if (len < 1) return false;
  const auto encoder_type = static_cast<OutputEncoderType>(data[0]);
  if (encoder_type != OutputEncoderType::As5600) {
    return false;
  }
  out_encoder_type = encoder_type;
  return true;
}

bool decodeFocCalibrationCmd_OptionA(const uint8_t data[8], uint8_t len) {
  if (len < 1) return false;
  return data[0] == 1;
}

bool decodeActuatorVoltageLimitCmd_OptionA(const uint8_t data[8],
                                           uint8_t len,
                                           float& out_voltage_limit) {
  return decodeInt32MUnits_(data, len, out_voltage_limit);
}

bool encodeActuatorVoltageLimitStatus_OptionA(float voltage_limit,
                                              uint8_t data[8],
                                              uint8_t& out_len) {
  return encodeInt32MUnits_(voltage_limit, data, out_len);
}

} // namespace
