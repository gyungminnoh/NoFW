# 배포 설정 검증 보고서 2026-04-25

## 1. 목적

다음 세 가지를 실제 연결된 하드웨어에서 확인했다.

- 배포표 기준 `CAN_NODE_ID` 변경 후 보드가 해당 ID로 정상 응답하는지
- steering / driving / auxiliary angle actuator 대표 설정이 실제로 저장되고 동작하는지
- 현재 코드 안에 배포 과정과 충돌하는 하드코딩이나 더 이상 쓰지 않는 항목이 있는지

이번 검증은 현재 연결된 보드 `1`대를 사용해 대표 설정을 순차적으로 업로드하는 방식으로 진행했다.
즉 `9`대 동시 버스 검증이 아니라, 대표 역할별 설정 검증이다.

## 2. 코드 정리 및 충돌 점검

이번 세션에서 정리한 항목:

- [tools/can_ui/smoke_test.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/smoke_test.py)
  - 세션 reset 검증이 항상 `node_id = 7`을 사용하던 하드코딩을 제거했다
  - 이제 전달된 `--can-iface`, `--node-id`를 그대로 사용한다
- [tools/can_ui/static/app.js](/home/gyungminnoh/projects/NoFW/NoFW/tools/can_ui/static/app.js)
  - profile 변경 대기 메시지에 하드코딩된 `0x5F7`를 제거했다
  - 현재 세션의 `node_id`에 맞는 runtime diag ID를 표시하도록 수정했다
- [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h)
  - 실제로 사용되지 않던 `ACTUATOR_BOOT_OUTPUT_DEG` 상수를 제거했다

추가 검토 결과:

- 메인 제어 경로에서 `CAN node_id`, profile, gear ratio, travel limit 저장 경로는 서로 충돌하지 않았다
- `output_min_deg/output_max_deg`는 저장 후 런타임에 반영되고 있었다
- auxiliary angle actuator 설정에서 현재 출력각이 하한 밖(`0 deg` 아래)일 때 음수 각도 명령이 일부 허용된 것처럼 보였지만,
  이는 버그가 아니라 "현재 이미 범위 밖이면 더 바깥쪽 명령은 막고, 안쪽 복귀 명령은 허용"하는 의도된 로직이었다

## 3. 대표 하드웨어 검증

### 3.1 Steering representative

- firmware `CAN_NODE_ID = 2`
- runtime config:
  - profile: `As5600`
  - gear ratio: `50.0`
  - limits: `-120.0 ~ 120.0 deg`

확인 결과:

- 세션 `node_id = 2`로 live 상태 확인
- `As5600` profile 저장/활성화 정상
- angle step `+8 deg` 테스트 정상

측정값:

- initial: `37.844 deg`
- target: `45.844 deg`
- final: `45.799 deg`
- overshoot: `0.0 deg`
- first within `1 deg`: `1.446 s`

### 3.2 Driving representative

- firmware `CAN_NODE_ID = 18`
- runtime config:
  - profile: `VelocityOnly`
  - gear ratio: `78 / 15 = 5.2`

확인 결과:

- 세션 `node_id = 18`로 live 상태 확인
- `VelocityOnly` profile 저장/활성화 정상
- `enable_output_angle_mode = false`, `enable_velocity_mode = true` 확인
- `candump`에서 실제 telemetry 확인:
  - velocity status: `0x412`
  - runtime diag: `0x602`
  - limits status: `0x432`
  - config status: `0x442`
- velocity step `+40 deg/s` 테스트 정상

측정값:

- tail average: `38.748 deg/s`
- tail error: `-1.252 deg/s`
- overshoot: `1.072 deg/s`
- first within `5%`: `1.225 s`

### 3.3 Auxiliary angle actuator representative

- firmware `CAN_NODE_ID = 31`
- runtime config:
  - profile: `As5600`
  - gear ratio: `30.0`
  - limits: `0.0 ~ 90.0 deg`

확인 결과:

- 세션 `node_id = 31`로 live 상태 확인
- `As5600` profile 저장/활성화 정상
- `candump`에서 실제 telemetry 확인:
  - angle status: `0x41F`
  - velocity status: `0x42F`
  - runtime diag: `0x60F`
  - limits status: `0x43F`
  - config status: `0x44F`
- 현재 출력각이 하한 밖에서 시작했지만, 안쪽 복귀 명령은 정상 수행
- 바깥쪽 음수 명령은 사실상 차단됨을 확인

측정값:

- inward recovery step:
  - initial: `-44.648 deg`
  - target: `-38.648 deg`
  - final: `-38.691 deg`
  - overshoot: `0.0 deg`
- outward blocked test:
  - initial: `-38.695 deg`
  - command: `-60.0 deg`
  - final: `-38.696 deg`
  - max outward motion: `-0.006 deg`

해석:

- 하한 밖에서 더 음수 방향으로 가는 명령은 막혔다
- 하한 쪽으로 복귀하는 명령만 허용되는 현재 설계와 일치한다

## 4. 복원 상태

검증 후 현재 연결된 보드는 개발 기본 상태로 복원했다.

- firmware `CAN_NODE_ID = 7`
- profile: `As5600`
- gear ratio: `8.0`
- limits: `-1080.0 ~ 1080.0 deg`
- `armed = false`

복원 후 확인:

- `candump`에서 `0x407`, `0x417`, `0x5F7`, `0x427`, `0x437` 정상 수신
- `python3 tools/can_ui/smoke_test.py --use-running-server --port 8765 --can-iface can0 --node-id 7`
  - `30/30 PASS`

## 5. 결론

- 배포표 기준 representative `node_id`와 설정은 현재 펌웨어에서 정상적으로 적용된다
- `VelocityOnly`, `As5600`, gear ratio, travel limit 저장 경로는 실기에서 동작했다
- 다중 보드 배포에 방해되는 테스트/UI 하드코딩 `node_id = 7` 경로는 제거했다
- 현재 연결된 보드는 다시 개발 기본 상태로 복원됐다
