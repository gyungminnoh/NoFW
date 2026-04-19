# CAN Architecture

Layers and responsibilities:

1) Transport (hardware access)
   - Files: include/can_transport.h, src/can_transport.cpp
   - Owns CAN pin setup, bitrate start, and raw RX frame fetch.

2) Protocol (payload definition)
   - Files: include/can_protocol.h, src/can_protocol.cpp
   - Defines CAN IDs and encoding/decoding rules (e.g., gripper command).

3) Service (application policy)
   - Files: include/can_service.h, src/can_service.cpp
   - Pulls frames from transport, decodes using protocol, updates GripperAPI.

Entry point:
   - src/main.cpp calls CanService::init() once and CanService::poll() in loop().

Namespace mapping:
   - Transport: CanTransport
   - Protocol: CanProtocol
   - Service: CanService

Gripper command frame (Option A):
   - Std ID: CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID (e.g., base+7 when CAN_NODE_ID=7)
   - Payload: data[0..1] = int16 little-endian centi-percent (0.01 %)
   - Range: 0.00 .. 100.00 (open %; 0 = fully open, 100 = fully closed)
   - Direction: if GRIP_INVERT_DIR=true, the meaning is inverted
   - Example: 25.00 % -> 2500 (0x09C4) -> data[0]=0xC4, data[1]=0x09

Gripper position report (Option A):
   - Std ID: CAN_ID_GRIP_POS_BASE + CAN_NODE_ID (e.g., base+7 when CAN_NODE_ID=7)
   - Payload: data[0..1] = int16 little-endian centi-percent (0.01 %)
   - Signal: current gripper open percent (boot-centered, clamped to [0, 100])
   - Direction: if GRIP_INVERT_DIR=true, the meaning is inverted
   - Period: CAN_POS_TX_MS in include/board_config.h (0=disable)

Timeout behavior:
   - CAN timeout constant: CAN_TIMEOUT_MS in include/board_config.h
   - Current policy: "HOLD-CURRENT" (target is set to current output)
   - One-shot: target is captured once when timeout is first detected
   - Hysteresis: timeout clears only after RX is stable for RX_RECOVER_MS
   - Timeout logic is active only after the first valid command is received
