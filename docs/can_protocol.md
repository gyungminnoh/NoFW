# CAN Frame Protocol

외부 컨트롤러가 현재 펌웨어와 통신할 때 맞춰야 하는 CAN 프레임 형식입니다.

## Bus

- Bitrate: `1 Mbps`
- Frame type: `11-bit standard ID`
- Node ID: `CAN_NODE_ID` 기본값 `7`
- Endianness: `little-endian`

## Frame IDs

- 명령 RX: `CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID`
- 위치 TX: `CAN_ID_GRIP_POS_BASE + CAN_NODE_ID`
- 기본값 기준:
  - RX ID = `0x200 + 7 = 0x207`
  - TX ID = `0x400 + 7 = 0x407`

## Payload

두 프레임 모두 2바이트 signed integer `centi-percent`를 사용합니다.

```text
cp = int16(data[0] | (data[1] << 8))
percent = cp * 0.01
```

## Gripper Command

- ID: `CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID`
- DLC: `2`
- 의미: 목표 gripper open percent
- 범위: `0.00 .. 100.00`
- 해석: `0 = fully open`, `100 = fully closed`
- 비고: 펌웨어 내부에서 수신값은 `[0, 100]`으로 clamp됨

예시:

- `25.00%` -> `2500` -> `C4 09`
- `100.00%` -> `10000` -> `10 27`

## Gripper Position Report

- ID: `CAN_ID_GRIP_POS_BASE + CAN_NODE_ID`
- DLC: `2`
- 의미: 현재 모터 위치로부터 계산한 gripper open percent
- 비고: 송신 전 clamp하지 않으므로 nominal range를 잠시 벗어날 수 있음
- 송신 주기: `CAN_POS_TX_MS` 기본값 `50 ms`

## Timeout

- 첫 유효 명령을 받은 이후에만 timeout 로직이 활성화됨
- `CAN_TIMEOUT_MS`를 넘기면 목표값을 현재 위치로 고정
- 짧은 RX recovery window 이후 timeout 상태 해제
