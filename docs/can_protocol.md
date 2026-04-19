# CAN Frame Protocol (Gripper FW)

This document describes the on-wire CAN frames used by this firmware so an
external controller can command and read gripper position.

## Bus and addressing

- Bitrate: 1 Mbps (see `include/board_config.h`)
- Frame type: 11-bit standard ID
- Node ID: `CAN_NODE_ID` (default: 7)
- Endianness: little-endian for multi-byte fields

## Frame IDs

- Gripper command (RX): `CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID`
- Gripper position report (TX): `CAN_ID_GRIP_POS_BASE + CAN_NODE_ID`
- Base IDs: `CAN_ID_GRIP_CMD_BASE = 0x200`, `CAN_ID_GRIP_POS_BASE = 0x400`

Example with `CAN_NODE_ID = 7`:

- Command ID: `CAN_ID_GRIP_CMD_BASE + 7`
- Position ID: `CAN_ID_GRIP_POS_BASE + 7`

## Payload definitions (Option A)

All payloads use a 2-byte signed integer in centi-percent.

```
int16 cp = (int16)(data[0] | (data[1] << 8));  // centi-percent
percent = cp * 0.01
```

### Gripper command (RX)

- ID: `CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID`
- DLC: 2
- Payload: `int16` centi-percent (0.01%)
- Meaning: target gripper open percent (0 = open, 100 = closed)
- Direction: if `GRIP_INVERT_DIR` is true, the meaning is inverted

Example:

- 25.00% -> `cp = 2500` -> bytes: `C4 09`
- 100.00% -> `cp = 10000` -> bytes: `10 27`

### Gripper position report (TX)

- ID: `CAN_ID_GRIP_POS_BASE + CAN_NODE_ID`
- DLC: 2
- Payload: `int16` centi-percent (0.01%)
- Meaning: current gripper open percent (boot-centered, clamped to [0, 100])
- Direction: if `GRIP_INVERT_DIR` is true, the meaning is inverted
- Period: `CAN_POS_TX_MS` (default 50 ms, 0 disables TX)

## Timeout behavior (receiver)

- If no valid command arrives for `CAN_TIMEOUT_MS` (default 100 ms), the
  controller holds its current output angle (one-shot capture on timeout entry).
- Timeout clears after a short RX recovery window (50 ms of stable RX).
- Timeout logic is active only after the first valid command is received.

## Notes

- Commands are interpreted as absolute open percent, not delta.
- Output is clamped to [0, 100] in firmware.
