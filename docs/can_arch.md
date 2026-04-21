# CAN Architecture

이 펌웨어의 CAN 처리는 세 계층으로 나뉩니다.

1. Transport
   - 파일: `include/can_transport.h`, `src/can_transport.cpp`
   - 역할: CAN 하드웨어 시작, raw frame 수신, 표준 프레임 송신

2. Protocol
   - 파일: `include/can_protocol.h`, `src/can_protocol.cpp`
   - 역할: CAN ID 정의, payload 인코딩/디코딩

3. Service
   - 파일: `include/can_service.h`, `src/can_service.cpp`
   - 역할: 수신 명령을 `GripperAPI`에 반영하고, 위치 보고와 timeout 정책을 수행

메인 제어 루프에서는 `src/main.cpp`가 `CanService::init()`를 한 번 호출하고, 이후 `loop()`에서 `CanService::poll()`을 반복 호출합니다.

## 현재 정책

- 명령 수신 ID: `CAN_ID_GRIP_CMD_BASE + CAN_NODE_ID`
- 위치 송신 ID: `CAN_ID_GRIP_POS_BASE + CAN_NODE_ID`
- 명령 값: `0..100%` open percent
- 위치 값: 현재 모터 위치 기반 open percent
- timeout: 첫 유효 명령 수신 이후, `CAN_TIMEOUT_MS`를 넘기면 현재 위치 유지
- 위치 송신 주기: `CAN_POS_TX_MS`

세부 on-wire 형식은 [can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)를 참고하면 됩니다.
