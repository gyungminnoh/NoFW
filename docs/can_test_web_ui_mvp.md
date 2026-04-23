# CAN 테스트 웹 UI MVP 명세

## 목적

사용자가 터미널에서 직접 `cansend`, `candump`를 다루지 않고도,
현재 메인 펌웨어가 CAN으로 제공하는 동작을 브라우저에서 직관적으로 시험할 수 있게 한다.

이 UI의 1차 목표는 다음과 같다.

- profile 변경
- power stage `arm/disarm`
- 출력축 angle command
- 출력축 velocity command
- angle / velocity / runtime diag 상태 확인
- 수동 테스트 중 이벤트와 송신 명령 로그 확인

## 범위

이 MVP는 현재 펌웨어가 이미 CAN으로 노출한 기능만 다룬다.

포함:

- `0x220 + node_id` profile 변경
- `0x230 + node_id` arm/disarm
- `0x200 + node_id` angle command
- `0x210 + node_id` velocity command
- `0x400 + node_id` angle status
- `0x410 + node_id` velocity status
- `0x5F0 + node_id` runtime diag

제외:

- `output_min_deg`, `output_max_deg` 변경
- `gear_ratio` 변경
- zero save / calibration clear 같은 고급 설정
- FRAM 설정 편집기

## 중요한 설계 제약

현재 펌웨어는 `CAN_TIMEOUT_MS = 100 ms` 정책을 사용한다.

즉 angle/velocity command를 한 번만 보내면:

- angle mode는 곧 current-angle hold로 들어갈 수 있고
- velocity mode는 곧 `0 deg/s`로 떨어질 수 있다

그래서 UI는 단발 송신기가 아니라,
현재 target을 내부에서 약 `20 Hz`로 반복 송신하는 latched command stream을 가져야 한다.

## UI 요구사항

### 연결 / 세션

- `can` 인터페이스 입력
- `node_id` 입력
- 현재 세션 적용 버튼
- 현재 세션 상태 표시

### 상태 표시

- 현재 출력축 angle
- 현재 출력축 velocity
- runtime diag raw frame
- stored profile
- active profile
- default control mode
- `need_calibration`
- `armed`
- `enable_output_angle_mode`
- `enable_velocity_mode`
- 최근 frame 수신 시각

### 제어 패널

- profile 선택 버튼
  - `VelocityOnly`
  - `As5600`
  - `TmagLut`
  - `DirectInput`
- `arm`
- `disarm`
- angle 입력 + 송신
- velocity 입력 + 송신
- `Hold current angle`
- `Zero velocity`
- `Stop streaming`
- optional raw frame 전송 패널

버튼 동작 정책:

- 현재 상태에서 수행할 수 없는 동작은 disabled로 보여야 한다
- 예:
  - 이미 armed면 `Arm` 버튼은 disabled
  - 이미 disarmed면 `Disarm` 버튼은 disabled
  - 현재 profile이 angle mode를 허용하지 않으면 angle 관련 버튼은 disabled
  - 현재 profile이 velocity mode를 허용하지 않으면 velocity 관련 버튼은 disabled
  - link가 죽어 있으면 대부분의 제어 버튼은 disabled

버튼 시각 정책:

- 현재 active profile 버튼은 눌린 상태처럼 보여야 한다
- profile 변경을 누르면 다음 `0x5F7`에서 stored/active profile이 요청값과 일치할 때까지 pending 상태를 보여야 한다
- profile 변경이 일정 시간 안에 반영되지 않으면 현재 active profile과 함께 실패 메시지를 보여야 한다
- 현재 latched stream이 angle인지 velocity인지 해당 버튼이 눌린 상태처럼 보여야 한다
- preset 버튼도 현재 입력값과 일치하면 눌린 상태처럼 보여야 한다

### 로그

- 최근 송신 명령 로그
- 최근 상태 변화 로그
- 오류 로그

## 백엔드 요구사항

- Python 표준 라이브러리만 사용
- `cansend`, `candump` subprocess 기반
- background monitor thread:
  - `0x407`
  - `0x417`
  - `0x5F7`
  를 감시
- background tx thread:
  - active angle/velocity target이 있으면 약 `50 ms`마다 반복 송신
- HTTP JSON API 제공

## 안전 정책

- `disarm`은 active stream을 함께 정지해야 한다
- UI는 사용자가 마지막으로 보낸 target을 계속 재전송하고 있음을 화면에 보여줘야 한다
- 큰 command를 보내기 전에 작은 command로 방향 확인을 권장해야 한다
- runtime diag에서 `armed` bit를 명시적으로 보여줘야 한다

## 구현 위치

- 서버:
  - `tools/can_ui/server.py`
- 정적 파일:
  - `tools/can_ui/static/`

## 실행 방식

예시:

```bash
python3 tools/can_ui/server.py --can-iface can0 --node-id 7 --port 8765
```

브라우저 접속:

```text
http://127.0.0.1:8765
```
