# CAN Architecture

현재 메인 펌웨어의 `CAN` 처리는 세 계층으로 나뉜다.

## 1. Transport

- 파일:
  - `include/can_transport.h`
  - `src/can_transport.cpp`
- 역할:
  - CAN 하드웨어 초기화
  - raw frame 수신
  - standard frame 송신

## 2. Protocol

- 파일:
  - `include/can_protocol.h`
  - `src/can_protocol.cpp`
- 역할:
  - frame ID 정의
- payload 인코딩/디코딩
- `deg` / `deg/s`를 `int32 mdeg` / `int32 mdeg/s`로 변환
- power-stage arm/disarm 명령 정의

## 3. Service

- 파일:
  - `include/can_service.h`
  - `src/can_service.cpp`
- 역할:
  - 수신 명령을 런타임 target으로 반영
  - angle command 수신 시 `OutputAngle`로 전환
  - velocity command 수신 시 `OutputVelocity`로 전환
  - power-stage arm/disarm 요청 수신
  - 주기적으로 angle/velocity status 송신
  - motor-side multi-turn 추정 기반 status 계산
  - timeout 정책 수행
- output profile 변경 요청 수신
- power-stage armed 상태를 runtime diagnostic에 반영

## Main Loop Interaction

메인 제어 루프는 [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp)에서 다음 순서로 `CAN`과 상호작용한다.

1. `CanService::init(actuator_config)` 호출
2. 각 loop에서 `CanService::poll(pos_mt)` 호출
3. `CanService::takePendingOutputProfileChange()`로 runtime profile 변경 요청 확인
4. `CanService::takePendingPowerStageEnable()`로 arm/disarm 요청 확인
5. 현재 `ActuatorAPI::active_control_mode`에 따라:
   - angle mode면 position outer loop 수행
   - velocity mode면 velocity command 직접 수행

여기서 중요한 점은:

- `pos_mt`는 입력축 `AS5048A`의 multi-turn 추정값이다
- output encoder는 매 loop feedback에 사용하지 않는다
- output encoder는 boot reference 정렬과 explicit zero capture에만 사용한다

## Current Policy

### Control Channels

- 출력축 각도 명령/상태:
  - `0x200 / 0x400 + node_id`
- 출력축 속도 명령/상태:
  - `0x210 / 0x410 + node_id`

angle와 velocity는 서로 다른 ID를 사용한다.

즉 현재 프로토콜은:

- 각도제어용 채널
- 속도제어용 채널

이 분리되어 있고, 마지막으로 받은 명령 종류에 따라 현재 제어 모드가 바뀐다.

### Profile Switching

- 출력 profile 변경 명령:
  - `0x220 + node_id`
- payload:
  - `data[0] = OutputEncoderType`

메인 펌웨어는 이 명령을 받아:

- 가능한 profile인지 검사하고
- runtime에 즉시 적용하고
- `FRAM`에 저장한다

현재 profile의 의미는 "런타임 폐루프 feedback 선택"이라기보다
"부팅 시 어떤 출력축 기준 경로를 사용할지"에 더 가깝다.

### Power Stage Arming

- power-stage arm/disarm 명령:
  - `0x230 + node_id`
- payload:
  - `data[0] = 0`:
    - disarm
  - `data[0] = 1`:
    - arm

메인 펌웨어는 부팅 직후 기본적으로 전력단을 올리지 않는다.

즉:

- 센서/FRAM/CAN 초기화는 먼저 수행
- gate enable과 `initFOC()`는 arm 명령을 받은 뒤에만 수행

### Runtime Diagnostic

- `0x5F0 + node_id`
- stored profile과 active profile을 동시에 보고
- calibration blocking 여부도 같이 보여 준다

이 프레임은 profile switching, fallback, calibration state 확인에 가장 먼저 봐야 하는 프레임이다.

## Why This Changed

예전 구조는 제품별 percentage 명령 중심이어서:

- 액추에이터 일반화가 어렵고
- `1:1` direct-drive 경로를 표현하기 어렵고
- 속도와 각도를 같은 퍼센트 의미로 다루는 문제가 있었다

현재 구조는 이를 다음처럼 정리한다.

- 제어 단위는 `deg`, `deg/s`
- output feedback/profile은 별도 개념
- 현재 제어 모드는 angle 또는 velocity로 분리
- velocity command는 내부 slew limit를 거쳐 적용
- velocity mode는 output angle edge braking을 사용하지 않음
- angle mode는 edge-braking cap과 최종 slew limit를 함께 사용
- `DirectInput` profile을 통해 `1:1` 경로도 angle-capable하게 지원
