# PID Tuning Report - 2026-04-23

## 대상

메인 펌웨어 `custom_f446re`의 현재 제어 구조:

- inner loop: SimpleFOC motor velocity PI
- outer loop: `PositionVelocityController` angle P controller
- output encoder profile: `As5600`
- runtime feedback/status: motor-side `AS5048A` multi-turn 기반 출력축 추정

## 튜닝 전 계수

```cpp
motor.PID_velocity.P = 0.12;
motor.PID_velocity.I = 0.4;
motor.PID_velocity.D = 0.0;
motor.LPF_velocity.Tf = 0.007;

pvc.Kp = 20.0f;
```

## 문제

작은 `+0.5 deg` angle step에서는 큰 문제가 보이지 않았지만, `+5 deg` angle step에서 overshoot가 과도했다.

기존 `pvc.Kp = 20`에서 측정:

- command: `+5 deg`
- overshoot: `4.418 deg`
- max absolute velocity: `19.391 deg/s`
- tail error: `-0.627 deg`

이 결과는 inner velocity PI보다 outer angle P gain이 너무 큰 것이 주요 원인으로 보였다.

## 튜닝 절차

`tools/control_tuning/can_step_response.py`를 추가해 동일 조건에서 반복 측정했다.

각 테스트는 다음 안전 절차를 따른다.

- 시작 전 `disarm`
- stream off
- `Hold Current`
- `arm`
- 작은 angle 또는 velocity command
- 측정
- `disarm`
- stream off
- 최종 상태 확인

## 테스트 결과

### `pvc.Kp = 5.0`

`+5 deg` step:

- overshoot: `0.114 deg`
- tail error: `0.088 deg`
- max absolute velocity: `10.657 deg/s`

`+10 deg` step:

- overshoot: `2.719 deg`
- tail error: `2.712 deg`
- max absolute velocity: `20.544 deg/s`

판정: `+5 deg`에서는 크게 개선됐지만 `+10 deg`에서는 아직 overshoot가 큼.

### `pvc.Kp = 3.0`

`+10 deg` step:

- overshoot: `1.228 deg`
- tail error: `1.202 deg`
- max absolute velocity: `15.161 deg/s`

판정: 개선됐지만 여전히 overshoot가 남음.

### `pvc.Kp = 2.0`

`+10 deg` step:

- overshoot: `0.000 deg`
- tail error: `-0.031 deg`
- max absolute velocity: `11.371 deg/s`

판정: 현재 테스트 범위에서 가장 안정적.

## Velocity Mode 확인

inner velocity PI는 일단 변경하지 않았다.

`pvc.Kp = 2.0` 적용 후 `+5 deg/s`, `3.0 s` velocity mode test:

- command: `5.000 deg/s`
- tail average: `4.633 deg/s`
- tail error: `-0.367 deg/s`
- max velocity: `6.207 deg/s`
- angle delta: `8.679 deg`

판정: 현재 저속 검증 범위에서는 inner velocity PI가 크게 실패하지 않는다.

## 추가 튜닝: angle command overshoot 재검증

사용자가 angle command에서 여전히 overshoot가 있다고 보고해, travel 범위 안에서 양방향과 더 큰 step을 추가 측정했다.

### `pvc.Kp = 2.0` 재검증

`+10 deg` step:

- overshoot: `0.083 deg`
- tail error: `0.069 deg`
- max absolute velocity: `11.206 deg/s`

`-10 deg` step:

- overshoot: `0.071 deg`
- tail error: `-0.044 deg`
- max absolute velocity: `10.657 deg/s`

`+30 deg` step:

- overshoot: `0.278 deg`
- tail error: `0.185 deg`
- max absolute velocity: `31.915 deg/s`

`-30 deg` step:

- overshoot: `0.271 deg`
- tail error: `-0.191 deg`
- max absolute velocity: `31.641 deg/s`

판정: 기존 대비 매우 좋아졌지만, overshoot 억제를 더 우선하려면 Kp를 더 낮출 여지가 있음.

### `pvc.Kp = 1.5` 재검증

`+30 deg` step:

- overshoot: `0.084 deg`
- tail error: `0.049 deg`
- max absolute velocity: `26.477 deg/s`

`-30 deg` step:

- overshoot: `0.039 deg`
- tail error: `-0.007 deg`
- max absolute velocity: `25.653 deg/s`

`+60 deg` step:

- overshoot: `0.170 deg`
- tail error: `0.077 deg`
- max absolute velocity: `48.230 deg/s`

판정: `pvc.Kp = 1.5`가 현재 overshoot 억제 기준에서는 더 적절하다.

### `pvc.Kp = 1.5` 대각도 재검증

`-60 deg` step:

- overshoot: `0.175 deg`
- tail error: `-0.094 deg`
- max absolute velocity: `48.395 deg/s`

`+120 deg` step:

- overshoot: `16.697 deg`
- tail error: `-0.006 deg`
- max absolute velocity: `81.958 deg/s`

판정: `±60 deg` 수준에서는 안정적이었지만, `+120 deg` 대각도 이동에서는 목표 근처 감속이 늦어 overshoot가 과도했다.

### `pvc.Kp = 1.0` 대각도 재검증

대각도 overshoot 억제를 우선해 outer angle P gain을 한 번 더 낮췄다.

`+120 deg` step:

- overshoot: `0.111 deg`
- tail error: `0.062 deg`
- max absolute velocity: `68.060 deg/s`

`-120 deg` step:

- target이 `output_min_deg = 0` 아래로 내려가 펌웨어가 정상적으로 `0 deg` 리밋에 클램프했다.
- 이 결과는 음방향 step 응답 판정값으로 사용하지 않는다.

`+100 deg` step:

- overshoot: `0.028 deg`
- tail error: `0.003 deg`
- max absolute velocity: `58.667 deg/s`

`-80 deg` step:

- overshoot: `0.112 deg`
- tail error: `-0.063 deg`
- max absolute velocity: `48.669 deg/s`

`+10 deg` step:

- overshoot: `0.019 deg`
- tail error: `0.002 deg`
- max absolute velocity: `6.702 deg/s`

판정: `pvc.Kp = 1.0`은 작은 step과 대각도 step 모두에서 overshoot와 tail error가 작다. 현재 검증 범위에서는 `pvc.Kp = 1.5`보다 더 안전한 설정이다.

## 적용한 최종 계수

```cpp
motor.PID_velocity.P = 0.12;
motor.PID_velocity.I = 0.4;
motor.PID_velocity.D = 0.0;
motor.LPF_velocity.Tf = 0.007;

pvc.Kp = 1.0f;
```

## 결론

현재 문제의 주된 원인은 velocity PI보다 outer angle P gain이었다.

`pvc.Kp`를 `20.0 -> 2.0 -> 1.5 -> 1.0`으로 낮춘 뒤, `+10 deg`, `+100 deg`, `+120 deg`, `-80 deg` step에서 overshoot가 충분히 작게 유지됐다.

추가 고속/대각도 튜닝 전에는 UI 또는 CAN status에서 `output_min_deg`, `output_max_deg`, gear ratio를 볼 수 있게 만드는 것이 우선이다. 특히 음방향 대각도 테스트는 `output_min_deg = 0` 클램프에 걸리지 않는 시작 위치와 목표 위치를 먼저 확인해야 한다.
