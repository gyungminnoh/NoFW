#pragma once
#include <Arduino.h>
#include "board_config.h"

// CAN protocol layer: frame IDs and payload encoding/decoding.
namespace CanProtocol {

  // RX command frame: target gripper open percent in centi-percent.
  static constexpr uint16_t CAN_ID_GRIP_CMD = CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID;

  // TX position frame: current gripper open percent in centi-percent.
  static constexpr uint16_t CAN_ID_GRIP_POS = CAN_ID_GRIP_POS_BASE + CAN_NODE_ID;

  bool decodeGripCmd_OptionA(const uint8_t data[8], uint8_t len, float& out_open_percent);
  bool encodeGripPos_OptionA(float open_percent, uint8_t data[8], uint8_t& out_len);

}
