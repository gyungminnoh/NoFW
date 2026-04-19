#pragma once
#include <Arduino.h>
#include "board_config.h"

// CAN protocol layer: frame IDs and payload encoding/decoding.
namespace CanProtocol {

  // Gripper command frame
  // Option A:
  //  - StdID CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID
  //  - data[0..1] = int16 little-endian centi-percent (0.01%)
  //  - 0.00 .. 100.00 (% open)
  static constexpr uint16_t CAN_ID_GRIP_CMD = CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID;

  // Gripper position report (actual output percent)
  // Option A:
  //  - StdID CAN_ID_GRIP_POS_BASE + CAN_NODE_ID
  //  - data[0..1] = int16 little-endian centi-percent (0.01%)
  //  - 0.00 .. 100.00 (% open)
  static constexpr uint16_t CAN_ID_GRIP_POS = CAN_ID_GRIP_POS_BASE + CAN_NODE_ID;

  bool decodeGripCmd_OptionA(const uint8_t data[8], uint8_t len, float& out_open_percent);
  bool encodeGripPos_OptionA(float open_percent, uint8_t data[8], uint8_t& out_len);

}
