#pragma once
#include <Arduino.h>
#include "board_config.h"
#include "config/actuator_types.h"

// CAN protocol layer: frame IDs and payload encoding/decoding.
namespace CanProtocol {

  uint16_t outputAngleCmdCanId(uint8_t node_id);
  uint16_t outputAngleStatusCanId(uint8_t node_id);
  uint16_t outputVelocityCmdCanId(uint8_t node_id);
  uint16_t outputVelocityStatusCanId(uint8_t node_id);
  uint16_t outputProfileCmdCanId(uint8_t node_id);
  uint16_t powerStageCmdCanId(uint8_t node_id);

  bool decodeOutputAngleDeg_OptionA(const uint8_t data[8], uint8_t len, float& out_angle_deg);
  bool encodeOutputAngleDeg_OptionA(float angle_deg, uint8_t data[8], uint8_t& out_len);
  bool decodeOutputVelocityDegPerSec_OptionA(const uint8_t data[8],
                                             uint8_t len,
                                             float& out_velocity_deg_s);
  bool encodeOutputVelocityDegPerSec_OptionA(float velocity_deg_s,
                                             uint8_t data[8],
                                             uint8_t& out_len);
  bool decodeOutputProfileCmd_OptionA(const uint8_t data[8],
                                      uint8_t len,
                                      OutputEncoderType& out_profile);
  bool decodePowerStageCmd_OptionA(const uint8_t data[8],
                                   uint8_t len,
                                   bool& out_enable);

}
