# 상위 제어기 연동 가이드

이 문서는 `Jetson` 같은 상위 제어기가 이 펌웨어가 올라간 모터드라이버를 제어할 때
반드시 알아야 하는 계약 사항을 정리한 문서다.

목표는 다음 세 가지다.

- 상위 제어기가 무엇을 명령할 수 있는지 명확히 한다
- 어떤 상태를 읽고 무엇을 믿어야 하는지 정리한다
- 부팅, profile 변경, arm/disarm, 정상 운전, 오류 대응의 기본 절차를 정의한다

## 1. 상위 제어기가 제어하는 대상은 무엇인가

이 펌웨어의 외부 제어 좌표계는 항상 출력축 기준이다.

즉 상위 제어기는 다음 두 채널만 알면 된다.

- 출력축 각도 command/status: `deg`
- 출력축 속도 command/status: `deg/s`

온와이어 인코딩은 둘 다 little-endian `int32`이고 스케일은 `0.001`이다.

- angle payload = `mdeg`
- velocity payload = `mdeg/s`

## 2. 상위 제어기가 반드시 알아야 하는 핵심 정책

### 2.1 Boot-safe 정책

- 보드 전원을 넣어도 power stage는 자동으로 올라오지 않는다
- 기본 상태는 `disarmed`다
- 실제 모터 구동 전에는 반드시 별도의 `arm` 명령이 필요하다
- 출력축 절대 기준을 읽는 profile에서는 부팅 기준 정렬 성공 시 초기 angle target이 `0 deg`로 잡힌다
- 그러므로 상위 제어기가 부팅 직후 별도 target을 보내지 않고 arm하면, 액추에이터는 저장된 출력축 원점으로 이동하려고 한다

즉 상위 제어기는:

1. 보드가 살아 있는지 확인하고
2. profile/calibration 상태를 확인하고
3. 명시적으로 `arm`한 뒤
4. 그 다음에만 angle/velocity command를 보내야 한다

### 2.2 Profile과 런타임 feedback은 같은 개념이 아니다

현재 profile의 의미는 주로:

- 부팅 시 출력축 `0 deg` 기준을 어떤 경로로 맞출지
- 어떤 제어 모드를 허용할지

를 결정하는 것이다.

하지만 런타임 FOC와 CAN status는 현재 코드 기준으로:

- 입력축 `AS5048A` multi-turn 추정

을 사용한다.

즉 상위 제어기는 다음을 구분해야 한다.

- `profile`:
  - boot-time output reference 경로
  - angle/velocity mode 허용 여부
- `runtime status 0x407 / 0x417`:
  - output-axis 단위로 변환된 motor-side multi-turn 추정값

### 2.3 Angle command는 travel limit로 clamp된다

현재 저장된:

- `output_min_deg`
- `output_max_deg`

바깥으로 큰 angle command를 보내도 내부 target은 clamp된다.
단, 현재 출력각 자체가 이미 저장 범위 밖이면 정책이 조금 다르다.

- 더 바깥쪽으로 가는 명령은 현재 위치 hold로 제한된다
- 현재 위치 hold 명령은 자동으로 travel edge로 끌려가지 않는다
- 범위 안쪽으로 돌아오는 명령은 허용된다

즉 상위 제어기는:

- 큰 각도 명령을 보냈는데 기대한 각도까지 안 갔다면
- 먼저 travel limit clamp를 의심해야 한다

### 2.4 Velocity mode와 Angle mode 정책이 다르다

- velocity mode:
  - 출력축 속도 명령을 따른다
  - 내부 slew limit는 적용된다
  - 현재 정책상 angle travel edge 기준 braking은 따로 하지 않는다
- angle mode:
  - 출력축 각도 목표를 outer loop로 추종한다
  - travel edge 근처에서 edge-braking cap이 적용된다
  - 그 뒤 최종 velocity command에도 추가 slew limit가 한 번 더 걸린다

즉 상위 제어기는:

- velocity mode를 "속도 우선 채널"
- angle mode를 "목표 위치 우선 채널"

로 이해하는 편이 맞다.

### 2.5 Timeout 정책

현재 `CAN_TIMEOUT_MS = 100 ms`다.

첫 유효 제어 명령을 받은 뒤 timeout 로직이 활성화된다.

- 현재 모드가 `OutputAngle`이면:
  - timeout 시 현재 각도를 hold target으로 잡는다
- 현재 모드가 `OutputVelocity`이면:
  - timeout 시 목표 속도를 `0 deg/s`로 강제한다

즉 상위 제어기는 명령을 단발로 던지는 것이 아니라,
운전 중에는 주기적으로 command를 갱신해야 한다.

권장:

- 최소 `10 Hz`보다 충분히 빠르게 보낸다
- 실사용에서는 `20 Hz` 이상으로 보내는 편이 안전하다

## 3. CAN 인터페이스에서 상위 제어기가 알아야 할 것

기본 `node_id = 7`이면 다음 ID를 사용한다.

- angle cmd: `0x207`
- angle status: `0x407`
- velocity cmd: `0x217`
- velocity status: `0x417`
- actuator limits status: `0x427`
- actuator config status: `0x437`
- profile cmd: `0x227`
- power-stage cmd: `0x237`
- actuator limits config cmd: `0x247`
- actuator gear config cmd: `0x257`
- output encoder config cmd: `0x267`
- output encoder direction auto-cal cmd: `0x277`
- runtime diag: `0x5F7`

버스 조건:

- `1 Mbps`
- `11-bit standard ID`
- little-endian

주의:

- 부팅 시 펌웨어는 FRAM에 저장된 `can_node_id`를 검사하고,
  값이 현재 빌드의 `CAN_NODE_ID`와 다르면 FRAM 값을 펌웨어 값으로 갱신한다.
- 따라서 상위 제어기는 "현재 설치된 펌웨어 빌드의 node_id가 최종적으로 적용된다"는 전제로 운용해야 한다.
- 여러 보드 배포 절차는 [can_node_id_provisioning.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_node_id_provisioning.md)를 참고한다.

## 4. 상위 제어기 상태기계에서 꼭 가져가야 하는 개념

상위 제어기는 최소한 아래 상태를 자체적으로 관리하는 편이 좋다.

- `link_alive`
- `profile_known`
- `profile_ready`
- `armed`
- `control_mode`
- `streaming_ok`

권장 정의:

- `link_alive`:
  - 최근 `0x5F7` 또는 status frame이 보이는가
- `profile_known`:
  - `0x5F7`에서 stored/active profile을 읽었는가
- `profile_ready`:
  - 원하는 profile이 active로 적용되었고 `need_calibration = 0`인가
- `armed`:
  - `0x5F7 data[7] bit1 == 1`인가
- `control_mode`:
  - 현재 펌웨어가 별도 active-mode status를 주지 않으므로
  - 상위 제어기가 마지막으로 성공적으로 보낸 command 종류를 기준으로 관리
- `streaming_ok`:
  - timeout을 피할 만큼 주기적으로 command를 보내고 있는가
- `travel_limits_known`:
  - `0x427`에서 `output_min_deg/output_max_deg`를 읽었는가
- `gear_ratio_known`:
  - `0x437`에서 gear ratio를 읽었는가

중요:

- `0x5F7 data[3]`은 현재 active control mode가 아니다
- 이것은 `default_control_mode`다
- 현재 active angle/velocity 모드는 상위 제어기가 마지막 제어 명령 종류로 관리해야 한다

## 5. 부팅 직후 상위 제어기의 권장 절차

권장 시퀀스:

1. `CAN` 링크가 살아 있는지 확인
2. `0x5F7`를 읽어 stored/active profile, `need_calibration`, armed bit 확인
3. `0x427`를 읽어 `output_min_deg/output_max_deg` 확인
4. `0x437`를 읽어 gear ratio 확인
5. 원하는 profile이 아니면 먼저 `0x237#00`으로 disarm하고 `0x227`로 변경 요청
6. 다시 `0x5F7`를 읽어 active profile이 바뀌었는지 확인
7. `As5600` profile에서 출력축 방향이 아직 검증되지 않은 보드라면 `0x277#01`로 방향 자동 캘리브레이션 실행
8. 필요한 경우에만 `0x237#01`로 arm
9. 작은 command로 방향과 반응 확인
10. 정상 확인 후 본 운전으로 들어감

권장 판정:

- `need_calibration = 1`이면 angle-capable profile 사용 전 보정 문제를 먼저 해결
- active profile이 기대값으로 바뀌지 않으면 상위 제어기는 운전을 시작하지 말아야 한다

## 6. Profile 변경 시 상위 제어기가 알아야 할 것

profile command:

- `0x227#00` = `VelocityOnly`
- `0x227#01` = `As5600`
- `0x227#02` = `TmagLut`
- `0x227#03` = `DirectInput`

행동 규칙:

- profile 변경은 메인 펌웨어가 즉시 적용을 시도한다
- 동시에 `FRAM`에 저장된다
- 별도 ack frame은 없다
- 결과는 `0x5F7`에서 확인해야 한다
- profile 변경은 `disarmed` 상태에서만 적용된다
- `As5600` 변경은 전환 시점에 AS5600 절대각을 읽을 수 있어야 성공한다

즉 상위 제어기는:

1. `0x237#00`으로 disarm
2. `0x5F7`에서 armed bit가 `0`인지 확인
3. profile command 전송
4. `0x5F7` 재확인
5. stored/active가 기대값인지 확인
6. 실패하면 `profile_select_result` 코드를 확인

순서로 처리해야 한다.

### Profile별 의미

#### `VelocityOnly`

- 출력축 절대각 기준 없음
- 속도제어만 가능
- angle command는 기대하지 말아야 한다

#### `As5600`

- `AS5600` 기반 boot zero/reference
- angle/velocity 모두 가능
- AS5600 zero offset과 방향 설정은 `FRAM`에 저장된다
- AS5600 장착 방향이 출력축 좌표계와 반대이면 `invert`가 켜져 있어야 한다
- 방향은 상위 제어기가 매번 알려줄 필요는 없고, 최초 세팅/정비 시 한 번 저장하면 된다
- 방향 자동 판정은 `0x270 + node_id` 명령으로 수행한다

#### `TmagLut`

- `TMAG LUT` 기반 boot zero/reference
- calibration이 선행되어야 함
- angle/velocity 모두 가능

#### `DirectInput`

- `gear_ratio == 1:1` 시스템용
- 입력축 엔코더를 출력축 엔코더로 직접 사용
- angle/velocity 모두 가능

## 7. Arm / Disarm 계약

power-stage command:

- `0x237#01` = arm
- `0x237#00` = disarm

상위 제어기가 알아야 할 점:

- arm 전에는 상태 프레임은 볼 수 있어도 실제 구동은 되지 않는다
- arm을 해야 gate enable과 `initFOC()`가 수행된다
- disarm하면 실제 전력단이 내려간다
- 현재 리비전에서는 arm 자체가 출력축 기준을 다시 틀어버리지 않도록 수정되어 있다

권장:

- 상위 제어기는 "명령 보냈으니 움직이겠지"라고 가정하지 말고
- `0x5F7` armed bit를 보고 arm 완료를 판단해야 한다

설정 변경 제약:

- `output_min_deg/output_max_deg` 변경은 disarmed 상태에서만 허용된다
- gear ratio 변경도 disarmed 상태에서만 허용된다
- profile 변경도 disarmed 상태에서만 허용된다
- output encoder 방향 설정과 방향 자동 캘리브레이션도 disarmed 상태에서만 허용된다
- armed 상태에서 설정/profile 변경 프레임을 보내도 펌웨어는 적용하지 않는다
- 상위 제어기는 설정 변경 전 반드시 `0x237#00`을 보내고 `0x5F7` armed bit가 `0`인지 확인해야 한다

## 8. 상위 제어기의 명령 채널 사용법

### 8.1 Angle command

- ID: `0x200 + node_id`
- payload: `int32 mdeg`

예:

- `10.000 deg` = `0x207#10270000`
- `0 deg` = `0x207#00000000`

사용 원칙:

- 처음에는 작은 각도 명령으로 방향 확인
- 큰 양/음의 값은 실제로는 travel limit에 의해 clamp될 수 있음
- 상위 제어기는 `0x427`의 `output_min_deg/output_max_deg`를 확인하고, 범위 밖 target을 보내지 않는 편이 좋음

### 8.2 Velocity command

- ID: `0x210 + node_id`
- payload: `int32 mdeg/s`

예:

- `10.000 deg/s` = `0x217#10270000`
- `0 deg/s` = `0x217#00000000`

사용 원칙:

- velocity mode는 내부 slew limit가 있으므로 step command가 즉시 같은 motor demand로 가지는 않는다
- 현재 정책상 angle travel edge를 기준으로 별도 braking 하지 않으므로 작은 명령부터 시작하는 편이 안전하다

## 9. 상위 제어기가 읽어야 하는 상태

### 9.1 `0x407`

- 현재 출력축 각도 상태
- 단위: `deg`
- 송신 주기 기본값: `50 ms`

### 9.2 `0x417`

- 현재 출력축 속도 상태
- 단위: `deg/s`
- 송신 주기 기본값: `50 ms`

### 9.3 `0x5F7`

- runtime diagnostic
- 송신 주기 기본값: `500 ms`

상위 제어기가 주로 볼 필드:

- `data[1]` = stored profile
- `data[2]` = active profile
- `data[4]` = velocity mode enable
- `data[5]` = angle mode enable
- `data[6]` bit0 = need_calibration
- `data[6]` bits4..7 = last profile-select result
- `data[7] bit1` = power stage armed

Profile-select result:

- `0` = `None`
- `1` = `Ok`
- `2` = `RejectedArmed`
- `3` = `As5600ReadFailed`
- `4` = `NotSelectable`
- `5` = `SaveFailed`

권장 해석:

- `0x407/0x417`:
  - 제어 상태 관찰용
- `0x5F7`:
  - 운전 가능 여부, profile 상태, arm 상태 확인용

### 9.4 `0x427`

- actuator travel limit status
- DLC: `8`
- `data[0..3] = output_min_deg`
- `data[4..7] = output_max_deg`
- 각 값은 `int32 mdeg`

상위 제어기는 angle command를 만들기 전에 이 범위를 확인해야 한다.
현재 펌웨어는 범위 밖 target을 내부에서 clamp할 수 있으므로, 상위 계층에서 먼저 막는 편이 더 직관적이다.

### 9.5 `0x437`

- actuator config status
- DLC: `8`
- `data[0..3] = gear_ratio * 1000`
- `data[4] = stored output_encoder_type`
- `data[5] = default_control_mode`
- `data[6] bit0 = enable_velocity_mode`
- `data[6] bit1 = enable_output_angle_mode`

gear ratio는 출력축 각도와 입력축/motor 각도를 해석할 때 필요한 기본 설정값이다.

## 10. 설정 변경 절차

### 10.1 Travel Limit 변경

명령:

- ID: `0x240 + node_id`
- 기본 node `7`: `0x247`
- payload:
  - `data[0..3] = output_min_deg` as `int32 mdeg`
  - `data[4..7] = output_max_deg` as `int32 mdeg`

권장 절차:

1. `0x237#00`으로 disarm
2. `0x5F7`에서 armed bit가 `0`인지 확인
3. `0x247`로 새 min/max 전송
4. `0x427`에서 값이 바뀌었는지 확인
5. 필요한 경우에만 다시 arm

### 10.2 Gear Ratio 변경

명령:

- ID: `0x250 + node_id`
- 기본 node `7`: `0x257`
- payload:
  - `data[0..3] = gear_ratio * 1000` as `int32`

권장 절차:

1. `0x237#00`으로 disarm
2. `0x5F7`에서 armed bit가 `0`인지 확인
3. `0x257`로 새 gear ratio 전송
4. `0x437`에서 gear ratio가 바뀌었는지 확인
5. `0x407`의 현재 출력각이 새 좌표계에서 기대 범위인지 확인
6. 필요한 경우에만 다시 arm

주의:

- gear ratio 변경은 출력축 좌표계 자체를 바꾼다
- `TmagLut` profile은 저장된 TMAG calibration의 learned gear ratio와 현재 gear ratio가 다르면 calibration required 상태로 간주된다
- `DirectInput` profile은 `gear_ratio == 1.000`일 때만 유효하다

### 10.3 AS5600 방향 수동 설정

명령:

- ID: `0x260 + node_id`
- 기본 node `7`: `0x267`
- payload:
  - `data[0] = 1` for `As5600`
  - `data[1] = invert`

예:

```bash
# AS5600 raw direction 그대로 사용
cansend can0 267#0100

# AS5600 raw direction 반전
cansend can0 267#0101
```

의미:

- 저장된 AS5600 zero offset은 유지한다
- AS5600 방향 플래그만 `FRAM`에 저장한다
- 적용 후 펌웨어는 boot reference를 다시 잡는다
- 이 값은 전원 재인가 후에도 유지되므로 매번 보낼 필요는 없다

### 10.4 AS5600 방향 자동 캘리브레이션

명령:

- ID: `0x270 + node_id`
- 기본 node `7`: `0x277`
- payload:
  - `data[0] = 1` for `As5600`

예:

```bash
cansend can0 277#01
```

적용 조건:

- power stage가 `disarmed` 상태여야 한다
- 현재 profile이 `As5600`이어야 한다
- 저장된 AS5600 zero calibration이 있어야 한다

펌웨어 내부 동작:

1. AS5600 raw angle을 읽는다
2. 잠시 power stage를 arm한다
3. 양의 출력축 방향으로 짧게 움직인다
4. AS5600 raw angle을 다시 읽는다
5. raw angle이 증가하면 `invert = 0`, 감소하면 `invert = 1`을 `FRAM`에 저장한다
6. power stage를 다시 disarm하고 boot reference를 다시 잡는다

상위 제어기 권장 절차:

1. `0x237#00`으로 disarm
2. `0x5F7`에서 armed bit가 `0`인지 확인
3. `0x277#01` 전송
4. `0x5F7`에서 다시 armed bit가 `0`으로 돌아왔는지 확인
5. 작은 angle command 또는 0deg 복귀로 방향을 최종 확인

주의:

- 이 명령은 짧지만 실제 모터를 움직인다
- 현재 펌웨어는 별도 성공/실패 ack frame을 보내지 않는다
- 상위 제어기는 명령 후 `0x407`, `0x417`, `0x5F7`를 보고 움직임과 disarm 복귀를 확인해야 한다

## 11. 상위 제어기에서 권장하는 최소 통합 절차

### 11.1 연결 시

1. `0x5F7` 수신 대기
2. `0x427`로 travel limit 확인
3. `0x437`로 gear ratio 확인
4. 원하는 profile과 calibration 상태 확인
5. 필요하면 profile 변경
6. `As5600` 보드가 처음 설치된 상태이면 `0x277#01`로 방향 자동 캘리브레이션
7. 다시 `0x5F7`로 적용 확인

### 11.2 구동 시작 시

1. `arm`
2. armed bit 확인
3. 작은 command 1회 전송
4. `0x407/0x417`로 반응 확인
5. 정상일 때만 본 운전 command stream 시작

### 11.3 구동 중

1. command를 timeout보다 빠르게 주기적으로 보냄
2. `0x407/0x417` 감시
3. `0x5F7` armed bit와 calibration 상태 감시

### 11.4 정지 시

1. velocity mode라면 먼저 `0 deg/s`
2. 필요하면 `disarm`
3. `0x5F7`에서 armed bit가 0으로 돌아왔는지 확인

## 12. 상위 제어기가 피해야 할 가정

- profile이 runtime output feedback source라고 가정하면 안 된다
- `0x5F7 data[3]`을 current active control mode라고 해석하면 안 된다
- board power-on만으로 모터가 구동 가능하다고 가정하면 안 된다
- 큰 angle command가 항상 그 값까지 도달한다고 가정하면 안 된다
- profile command에 별도 ack가 온다고 가정하면 안 된다
- output encoder 방향 자동 캘리브레이션에 별도 ack가 온다고 가정하면 안 된다

## 13. 통합 시 흔한 실패 원인

### profile 변경 후 동작이 기대와 다름

원인 후보:

- active profile이 실제로 안 바뀜
- armed 상태에서 profile 변경을 보냄
- `As5600ReadFailed`: AS5600 I2C/배선/자석 상태 문제
- calibration 미완료
- 현재 시스템이 `DirectInput` 조건이 아님

### 0deg 복귀 방향이 반대로 보임

원인 후보:

- AS5600 `invert` 설정이 현재 장착 방향과 맞지 않음
- AS5600 zero offset이 잘못 저장됨
- `gear_ratio` 또는 `motor_to_output_sign` 설정이 실제 기구와 맞지 않음

권장 대응:

1. disarm 상태 확인
2. `0x277#01`로 AS5600 방향 자동 캘리브레이션 실행
3. 그래도 맞지 않으면 manual zero 절차로 AS5600 zero를 다시 저장
4. 이후 작은 angle command로 방향 검증

### command를 보냈는데 멈춰 버림

원인 후보:

- command stream 간격이 `100 ms`를 넘겨 timeout 발생
- angle mode에서는 hold-current
- velocity mode에서는 target zero

### 전원은 살아 있는데 모터가 안 움직임

원인 후보:

- 아직 `disarmed`
- 상위 제어기가 arm bit 확인 없이 command만 보내고 있음

### 큰 angle command가 중간에서 멈춤

원인 후보:

- stored travel limit clamp
- 현재 출력각이 이미 stored travel limit 밖에 있어서, 더 바깥쪽 명령이 현재 위치 hold로 제한됨

## 14. 상위 제어기 구현 권장사항

- 명령 송신 스레드와 상태 수신 스레드를 분리한다
- `0x5F7` 기반의 명시적 상태기계를 둔다
- control mode는 마지막 command 종류로 상위 제어기에서 직접 추적한다
- timeout 대비를 위해 command stream 주기를 일정하게 유지한다
- emergency stop 경로는 최소한 `disarm`을 바로 보낼 수 있게 만든다

간단한 권장 우선순위:

1. `0x5F7` 읽기
2. profile 확인
3. arm
4. 작은 command
5. 정상 확인 후 본 운전
6. 종료 시 disarm
