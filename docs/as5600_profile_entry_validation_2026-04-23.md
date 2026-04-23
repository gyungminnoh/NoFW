# AS5600 Profile Entry Validation - 2026-04-23

## 목적

`As5600` profile 진입이 잘 되지 않는 문제와, 그 이후 작은 angle command 검증 중 발견된 큰 이동 문제를 확인하고 수정했다.

## 발견된 문제

### 1. 최초 `As5600` 진입 차단

기존 펌웨어는 `As5600` profile 선택 시 이미 저장된 `AS5600` zero/calibration이 있어야만 profile 선택을 허용했다.

결과적으로 물리 `AS5600` 센서가 정상이어도, 저장된 zero가 없는 보드에서는 `As5600` profile 최초 진입이 막힐 수 있었다.

### 2. 최초 진입 후 RAM zero 기준 미갱신

`As5600` 최초 진입 시 FRAM에는 새 zero가 저장됐지만, 실행 중인 RAM의 `ActuatorAPI::output_zero_ref_rad`가 즉시 갱신되지 않았다.

이 때문에 profile 변경 직후에는 새 zero 기준이 아니라 이전 기준으로 boot reference가 잡힐 수 있었다.

### 3. travel limit 밖에서 `Hold Current`가 edge 복귀로 바뀜

현재 출력각이 저장된 `output_min_deg`보다 작은 상태에서 `Hold Current`를 보내면, 기존 로직은 target을 먼저 `output_min_deg`로 clamp했다.

그 결과 현재 위치를 유지하려던 명령이 실제로는 `0 deg` 방향의 큰 이동 명령으로 바뀌었다.

## 적용한 수정

- `As5600` profile 요청 시 실제 `AS5600` read가 성공하면, 저장된 zero가 없을 때 현재 절대각을 첫 `0 deg` 기준으로 저장한다.
- profile 변경 시 선택된 profile의 zero를 RAM의 `ActuatorAPI::output_zero_ref_rad`에 즉시 반영한다.
- angle limit 정책을 수정했다.
- 현재 출력각이 travel 범위 안이면 기존처럼 target을 `output_min_deg .. output_max_deg`로 clamp한다.
- 현재 출력각이 이미 범위 밖이면 더 바깥쪽으로 가는 명령만 현재 위치 hold로 제한한다.
- 현재 위치 hold 또는 범위 안쪽으로 돌아오는 작은 명령은 허용한다.

## 검증 결과

### Profile 전환

- 명령: `cansend can0 227#01`
- 결과: `0x5F7#FB01010101010001`
- decoded:
  - stored profile: `As5600`
  - active profile: `As5600`
  - default control mode: `OutputAngle`
  - angle mode enabled: `true`
  - velocity mode enabled: `true`
  - armed: `false`

### Hold-current arm 검증

- 시작 상태:
  - angle: 약 `-87.009 deg`
  - armed: `false`
- 절차:
  - UI API로 `Hold Current`
  - `Arm`
  - 약 `1.5 s` 관측
- 결과:
  - angle window: `-87.020 .. -86.995 deg`
  - velocity window: 약 `-0.165 .. 0.494 deg/s`
  - 큰 자동 복귀 없음

### 작은 inward angle step 검증

- 시작 상태:
  - angle: 약 `-86.998 deg`
- 명령:
  - target: `-86.498 deg`
  - 현재 위치 기준 `+0.5 deg`
- 결과:
  - angle window: first `-87.020 deg`, last `-86.506 deg`
  - min/max: `-87.020 .. -86.479 deg`
  - velocity window: 약 `-0.659 .. 3.131 deg/s`
  - 최종 상태: `armed = false`, stream off

## 결론

`As5600` profile 최초 진입은 이제 저장된 zero가 없는 보드에서도 가능하다.

profile 변경 직후에도 RAM zero 기준이 즉시 갱신된다.

또한 현재 출력각이 travel limit 밖에 있을 때 `Hold Current`가 travel edge 복귀 명령으로 바뀌는 문제는 수정됐다.
