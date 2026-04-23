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

## 추가 튜닝: 반응 속도 개선

사용자가 수동 테스트 중 반응이 너무 느리다고 보고했다.
이 시점의 벤치 조건은 출력축에 부하/메커니즘이 연결되지 않은 상태이고, travel limit은 `-1080 .. 1080 deg`로 설정되어 있었다.

측정 스크립트에 다음 시간 지표를 추가했다.

- `t_90_s`: step 크기의 90% 지점에 처음 도달한 시간
- `first_within_1deg_s`: target의 `±1 deg` 안에 처음 들어온 시간
- `settle_within_1deg_s`: 이후 끝까지 `±1 deg` 안에 머문 첫 시간

### 기존 `pvc.Kp = 1.0`, angle slew `60 deg/s^2`

`-180 deg` step:

- overshoot: `0.579 deg`
- max absolute velocity: `93.988 deg/s`
- `t_90_s`: `3.570 s`
- `first_within_1deg_s`: `4.582 s`
- `settle_within_1deg_s`: `4.582 s`

판정: overshoot는 작지만, 체감 반응이 느릴 수 있다.

### `pvc.Kp = 1.5`, angle slew `180 deg/s^2`

`-180 deg` step:

- overshoot: `0.638 deg`
- tail error: `0.006 deg`
- max absolute velocity: `144.360 deg/s`
- `t_90_s`: `2.616 s`
- `first_within_1deg_s`: `3.416 s`
- `settle_within_1deg_s`: `3.416 s`

`+180 deg` step:

- overshoot: `1.332 deg`
- tail error: `-0.004 deg`
- max absolute velocity: `145.184 deg/s`
- `t_90_s`: `2.608 s`
- `first_within_1deg_s`: `3.407 s`
- `settle_within_1deg_s`: `6.184 s`

판정: 기존 `Kp = 1.0` 대비 90% 도달 시간이 약 `27%` 줄었고, 최고속도도 약 `50%` 증가했다. 한 방향에서 overshoot가 `1 deg`를 약간 넘지만 최종 오차는 거의 없다.

### `Kp` / angle slew 분리 재튜닝

사용자가 여전히 반응이 느리다고 보고해 `pvc.Kp`와 angle-mode slew를 분리해서 다시 측정했다.
테스트는 각 조합을 실제 업로드한 뒤 `+180 deg`, `-180 deg` step으로 수행했다.

| `pvc.Kp` | angle slew (`deg/s^2`) | avg overshoot (`deg`) | avg `t_90_s` | avg first `±1 deg` | avg settle `±1 deg` | 판정 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `1.5` | `180` | `1.045` | `2.613` | `3.414` | `4.695` | 기존 기준값 |
| `1.5` | `240` | `1.032` | `2.510` | `3.312` | `4.673` | slew 단독 증가는 개선폭이 작음 |
| `1.6` | `240` | `1.206` | `2.398` | `3.174` | `6.054` | 안정적이지만 체감 개선이 제한적 |
| `1.7` | `240` | `1.677` | `2.344` | `2.984` | `5.626` | 좋은 균형 |
| `1.7` | `300` | `1.698` | `2.291` | `2.930` | `5.547` | 최종 선택 |
| `1.8` | `240` | `2.014` | `2.295` | `2.854` | `5.280` | 빠르지만 overshoot가 약 `2 deg` |
| `1.8` | `180` | `5.277` | `2.288` | `2.607` | `4.870` | overshoot 과다로 기각 |

해석:

- `Kp = 1.5`에서 angle slew만 `180 -> 240`으로 올리면 `t_90`은 약 `0.10 s`만 줄었다.
- 반응 개선의 주된 요인은 slew보다 `pvc.Kp` 증가였다.
- 다만 `Kp = 1.8`은 overshoot가 `2 deg` 이상으로 올라가거나, slew 조합에 따라 `5 deg` 이상으로 악화됐다.
- 현재 벤치 기준 최종 균형점은 `Kp = 1.7`, angle slew `300 deg/s^2`다.

최종 선택 조합의 세부 측정:

`+180 deg` step:

- overshoot: `1.690 deg`
- tail error: `0.003 deg`
- max absolute velocity: `162.818 deg/s`
- `t_90_s`: `2.288 s`
- `first_within_1deg_s`: `2.928 s`
- `settle_within_1deg_s`: `5.655 s`

`-180 deg` step:

- overshoot: `1.706 deg`
- tail error: `0.028 deg`
- max absolute velocity: `168.420 deg/s`
- `t_90_s`: `2.293 s`
- `first_within_1deg_s`: `2.931 s`
- `settle_within_1deg_s`: `5.438 s`

기존 `Kp = 1.5`, angle slew `180 deg/s^2` 대비:

- 평균 `t_90_s`: `2.613 s -> 2.291 s`
- 평균 first `±1 deg`: `3.414 s -> 2.930 s`
- 평균 overshoot: `1.045 deg -> 1.698 deg`

판정: overshoot 증가를 약 `0.65 deg` 추가로 허용하는 대신, 눈으로 보이는 초기 반응과 `±1 deg` 최초 도달 시간이 유의미하게 빨라졌다.

## Velocity Mode slew 조정

velocity command도 수동 테스트에서 반응이 늦게 느껴질 수 있어 output-side velocity slew를 높였다.

변경:

- `ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 90.0 -> 180.0`

`+60 deg/s`, `4 s` velocity step:

- tail average: `59.277 deg/s`
- tail error: `-0.723 deg/s`
- max velocity: `60.700 deg/s`

판정: 저속/중속 velocity command 추종은 현재 벤치 기준에서 충분히 안정적이다.

## 적용한 최종 계수

```cpp
motor.PID_velocity.P = 0.12;
motor.PID_velocity.I = 0.4;
motor.PID_velocity.D = 0.0;
motor.LPF_velocity.Tf = 0.007;

pvc.Kp = 1.7f;
```

추가 반응속도 튜닝 후 최종 motion shaping:

```cpp
ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 = 180.0f;
ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 = 300.0f;

pvc.Kp = 1.7f;
```

## 결론

현재 문제의 주된 원인은 velocity PI보다 outer angle P gain이었다.

`pvc.Kp`를 `20.0 -> 2.0 -> 1.5 -> 1.0`으로 낮추면 overshoot는 매우 작아졌지만, 수동 테스트 반응은 느렸다.
이후 실제 업로드 기반 조합 비교에서는 `pvc.Kp = 1.7`, angle-mode slew `300 deg/s^2`, velocity-mode slew `180 deg/s^2`가 더 적절한 균형이었다.

추가 고속/대각도 튜닝 전에는 UI에서 현재 travel limit과 gear ratio를 확인하고, 명령 target이 범위 안에 있는지 먼저 확인해야 한다.
