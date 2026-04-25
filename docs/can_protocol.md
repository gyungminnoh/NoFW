# CAN Frame Protocol

이 문서는 현재 메인 펌웨어의 `CAN` 온와이어 형식을 설명한다.

이제 메인 펌웨어는 그리퍼 전용 `% open` 프로토콜이 아니라, 출력축 기준 액추에이터 프로토콜을 사용한다.

핵심 단위는 다음 두 가지다.

- 출력축 각도: `deg`
- 출력축 속도: `deg/s`

온와이어 payload는 모두 little-endian `int32` 고정소수점이며, 실제 스케일은 `0.001` 단위다.

즉:

- angle payload = `mdeg`
- velocity payload = `mdeg/s`

## Bus

- Bitrate: `1 Mbps`
- Frame type: `11-bit standard ID`
- Node ID: 기본값 `7`
- Endianness: `little-endian`

## Frame IDs

- 출력축 각도 명령 RX: `0x200 + node_id`
- 출력축 각도 상태 TX: `0x400 + node_id`
- 출력축 속도 명령 RX: `0x210 + node_id`
- 출력축 속도 상태 TX: `0x410 + node_id`
- actuator travel limit 상태 TX: `0x420 + node_id`
- actuator config 상태 TX: `0x430 + node_id`
- 출력 프로파일 변경 RX: `0x220 + node_id`
- power-stage arm/disarm RX: `0x230 + node_id`
- actuator travel limit 설정 RX: `0x240 + node_id`
- actuator gear ratio 설정 RX: `0x250 + node_id`
- FOC calibration RX: `0x290 + node_id`
- 런타임 진단 TX: `0x5F0 + node_id`

기본 `node_id = 7`이면:

- angle cmd = `0x207`
- angle status = `0x407`
- velocity cmd = `0x217`
- velocity status = `0x417`
- actuator limits status = `0x427`
- actuator config status = `0x437`
- profile cmd = `0x227`
- power-stage cmd = `0x237`
- actuator limits config cmd = `0x247`
- actuator gear config cmd = `0x257`
- FOC calibration cmd = `0x297`
- runtime diag = `0x5F7`

## Numeric Encoding

### Angle

```text
raw = int32 little-endian
angle_deg = raw * 0.001
```

예시:

- `50.000 deg` -> `50000` -> `50 C3 00 00`
- `-12.500 deg` -> `-12500` -> `2C CF FF FF`

### Velocity

```text
raw = int32 little-endian
velocity_deg_s = raw * 0.001
```

예시:

- `100.000 deg/s` -> `100000` -> `A0 86 01 00`
- `-25.000 deg/s` -> `-25000` -> `58 9E FF FF`

## Output Angle Command

- ID: `0x200 + node_id`
- DLC: `4`
- 의미: 목표 출력축 각도
- 단위: `deg`
- 인코딩: `int32 mdeg`

이 프레임을 수신하면 런타임 제어 모드는 `OutputAngle`로 전환된다.

단, 현재 profile이 각도제어를 허용하지 않으면 무시된다.

angle target 제한 정책:

- 현재 출력각이 `output_min_deg .. output_max_deg` 안에 있으면 target은 그 범위로 clamp된다
- 현재 출력각이 이미 범위 밖이면 더 바깥쪽으로 가는 target은 현재 위치 hold로 제한된다
- 현재 출력각이 이미 범위 밖이더라도 범위 안쪽으로 돌아오는 target은 허용된다

## Output Angle Status

- ID: `0x400 + node_id`
- DLC: `4`
- 의미: 현재 출력축 각도 추정값
- 단위: `deg`
- 인코딩: `int32 mdeg`
- 송신 주기: `CAN_STATUS_TX_MS` 기본값 `50 ms`

이 값은 부팅 시 정렬된 출력축 `0 deg` 기준을 바탕으로,
런타임에는 입력축 `AS5048A`의 motor multi-turn 추정으로 계산한 출력축 각도다.
즉 현재 메인 펌웨어는 output encoder를 런타임 각도 status 계산에 직접 사용하지 않는다.

## Output Velocity Command

- ID: `0x210 + node_id`
- DLC: `4`
- 의미: 목표 출력축 속도
- 단위: `deg/s`
- 인코딩: `int32 mdeg/s`

이 프레임을 수신하면 런타임 제어 모드는 `OutputVelocity`로 전환된다.

단, 현재 profile이 속도제어를 허용하지 않으면 무시된다.
또한 메인 펌웨어는 이 velocity target을 내부 slew limit를 통해 완만하게 적용한다.
즉 큰 step 명령이 와도 실제 motor-side velocity demand는 점진적으로 증가/감소한다.

## Output Velocity Status

- ID: `0x410 + node_id`
- DLC: `4`
- 의미: 현재 출력축 속도
- 단위: `deg/s`
- 인코딩: `int32 mdeg/s`
- 송신 주기: `CAN_STATUS_TX_MS` 기본값 `50 ms`

이 값도 런타임 output encoder 변화율이 아니라,
motor multi-turn 추정값의 시간차로 계산한 출력축 속도다.

## Actuator Limits Status

- ID: `0x420 + node_id`
- DLC: `8`
- 의미: 현재 펌웨어가 사용하는 출력축 travel limit
- 송신 주기: runtime diagnostic과 동일하게 기본 `500 ms`

Payload:

- `data[0..3] = output_min_deg`
- `data[4..7] = output_max_deg`

각 값은 `int32 mdeg` little-endian이다.

기본 `node_id = 7`에서 현재 `0 .. 2160 deg`라면:

```text
0x427#0000000080F52000
```

상위 제어기와 UI는 angle command를 보내기 전에 이 범위를 확인해야 한다.
범위 밖 angle target은 펌웨어 내부에서 clamp될 수 있다.

## Actuator Config Status

- ID: `0x430 + node_id`
- DLC: `8`
- 의미: 상위 제어기가 actuator 해석에 필요한 정적 설정 요약
- 송신 주기: runtime diagnostic과 동일하게 기본 `500 ms`

Payload:

- `data[0..3] = gear_ratio`
  - `int32` little-endian
  - scale `0.001`
  - 예: `8.000:1` -> `8000` -> `40 1F 00 00`
- `data[4] = stored output_encoder_type`
- `data[5] = default_control_mode`
- `data[6]` bitfield:
  - bit0 = `enable_velocity_mode`
  - bit1 = `enable_output_angle_mode`
- `data[7] = reserved`

기본 `node_id = 7`, `gear_ratio = 8.000`, `As5600`, `OutputAngle`, angle/velocity enabled이면:

```text
0x437#401F000001010300
```

## Output Profile Command

- ID: `0x220 + node_id`
- DLC: `1`
- 의미: 출력축 피드백/profile 변경 요청

Payload:

- `data[0] = OutputEncoderType`

현재 유효 값:

- `0` = `VelocityOnly`
- `1` = `As5600`
- `2` = `TmagLut`
- `3` = `DirectInput`

이 명령을 수신하면 메인 펌웨어는:

1. 요청 profile이 현재 하드웨어/보정 상태에서 허용되는지 검사하고
2. 가능하면 즉시 적용하고
3. `FRAM`의 `ActuatorConfig`에 저장한다

적용 조건:

- power stage가 `disarmed` 상태일 때만 적용된다
- `armed` 상태에서 보낸 profile command는 안전상 적용하지 않는다
- `As5600` profile은 전환 시점에 AS5600 절대각을 읽을 수 있어야 적용된다

결과 확인은 별도 ack 프레임 대신 runtime diagnostic frame `0x5F0 + node_id`를 사용한다.

## Power Stage Command

- ID: `0x230 + node_id`
- DLC: `1`
- 의미: 전력단 arm/disarm 요청

Payload:

- `data[0] = 0`:
  - disarm
  - gate disable 유지
  - FOC loop 미동작
- `data[0] = 1`:
  - arm
  - gate enable 후 `initFOC()` 수행
  - 이후에만 실제 모터 구동 가능

기본 정책:

- 부팅 직후 메인 펌웨어는 기본적으로 `disarmed` 상태다
- 즉, 보드 전원 인가만으로는 전력단을 자동으로 켜지 않는다
- 실제 구동 전에는 먼저 `0x230 + node_id`로 arm 명령을 보내야 한다
- `As5600`/`TmagLut`처럼 출력축 절대 기준을 읽는 profile에서는 부팅 기준 정렬 성공 시 초기 angle target이 `0 deg`다
- 따라서 부팅 직후 별도 angle command 없이 arm하면 저장된 출력축 원점으로 이동하려고 한다

예시:

```bash
# arm
cansend can0 237#01

# disarm
cansend can0 237#00
```

## Actuator Limits Config Command

- ID: `0x240 + node_id`
- DLC: `8`
- 의미: `output_min_deg`, `output_max_deg`를 `FRAM`에 저장하고 런타임 설정에 적용
- 적용 조건: power stage가 `disarmed` 상태일 때만 적용

Payload:

- `data[0..3] = output_min_deg`
- `data[4..7] = output_max_deg`

각 값은 `int32 mdeg` little-endian이다.

제약:

- 두 값은 finite number여야 한다
- `abs(value) <= 1,000,000 deg`
- `output_max_deg > output_min_deg`

예시:

```bash
# 0 .. 2160 deg
cansend can0 247#0000000080F52000
```

적용 후에는 `0x427`에서 저장/적용된 값을 확인한다.

## Actuator Gear Ratio Config Command

- ID: `0x250 + node_id`
- DLC: `4`
- 의미: gear ratio를 `FRAM`에 저장하고 출력축 좌표계를 재초기화
- 적용 조건: power stage가 `disarmed` 상태일 때만 적용

Payload:

- `data[0..3] = gear_ratio`
- 인코딩: `int32`, scale `0.001`

제약:

- `0.001 <= gear_ratio <= 1000.000`
- 현재 profile이 `DirectInput`이면 `gear_ratio == 1.000`일 때만 적용 가능

gear ratio 변경은 출력축 좌표계 해석을 바꾸므로, 적용 시 펌웨어는 boot reference를 다시 잡는다.
출력축 절대 기준을 읽을 수 있으면 초기 target은 `0 deg`가 되고, 그렇지 않으면 current-angle hold 상태로 돌아간다.

예시:

```bash
# 8.000:1
cansend can0 257#401F0000
```

적용 후에는 `0x437`에서 저장/적용된 gear ratio를 확인한다.

## FOC Calibration Command

- ID: `0x290 + node_id`
- 기본 node `7`이면 `0x297`
- DLC: `1`
- 의미: 저장된 FOC 값을 무시하고 `initFOC()`를 새로 수행한 뒤 trusted calibration slot에 저장
- 적용 조건: power stage가 `disarmed` 상태일 때만 적용

Payload:

- `data[0] = 1`

동작:

1. 펌웨어가 잠시 power stage를 enable한다.
2. 기존 FOC calibration을 재사용하지 않고 `sensor_direction = UNKNOWN`, `zero_electric_angle = NOT_SET`에서 `initFOC()`를 실행한다.
3. 성공하면 새 `sensor_direction`, `zero_electric_angle`을 FRAM trusted calibration slot에 저장한다.
4. 명령 종료 후 power stage를 다시 disarm한다.

이 명령은 FOC calibration만 갱신한다.
출력축 zero, AS5600 invert, profile, travel limits, gear ratio, node ID는 변경하지 않는다.

예시:

```bash
# node 7 FOC calibration
cansend can0 297#01
```

성공 여부는 runtime diagnostic `0x5F0 + node_id`에서 확인한다.

- `data[4] bit2 = 1`: trusted FOC calibration valid
- `data[5] = 0`: runtime fault 없음
- `data[7] bit1 = 0`: 명령 종료 후 disarmed

출력축 calibration이 별도로 없으면 FOC 성공 후에도 `need_calibration`은 계속 `1`일 수 있다.

## Output Encoder Config Command

- ID: `0x260 + node_id`
- 기본 node `7`이면 `0x267`
- DLC: `2`
- 의미: 출력축 엔코더의 장착 방향 설정을 `FRAM`에 저장하고 출력축 기준을 재초기화
- 적용 조건: power stage가 `disarmed` 상태일 때만 적용

Payload:

- `data[0] = output_encoder_type`
  - `1` = `As5600`
- `data[1] = invert`
  - `0` = AS5600 raw direction 그대로 사용
  - `1` = AS5600 raw direction 반전

AS5600 장착 방향이 출력축 좌표계와 반대이면 `invert = 1`로 저장해야 한다.
이 명령은 저장된 AS5600 zero offset은 유지하고 방향만 바꾼다.

예시:

```bash
# AS5600 invert off
cansend can0 267#0100

# AS5600 invert on
cansend can0 267#0101
```

## Output Encoder Direction Auto-Calibration Command

- ID: `0x270 + node_id`
- 기본 node `7`이면 `0x277`
- DLC: `1`
- 의미: 출력축 엔코더 방향을 자동 판정하고 `FRAM`에 저장
- 적용 조건:
  - power stage가 `disarmed` 상태일 때만 시작
  - 현재 profile이 `As5600`
  - 저장된 AS5600 zero calibration이 있어야 함

Payload:

- `data[0] = output_encoder_type`
  - `1` = `As5600`

동작:

1. 현재 AS5600 raw angle을 읽는다.
2. 펌웨어가 잠시 power stage를 arm한다.
3. 양의 출력축 방향으로 짧게 구동한다.
4. 이동 후 AS5600 raw angle을 다시 읽는다.
5. raw angle이 양의 방향으로 변했으면 `invert = 0`, 음의 방향으로 변했으면 `invert = 1`을 저장한다.
6. power stage를 다시 disarm하고 boot reference를 다시 잡는다.

예시:

```bash
# AS5600 방향 자동 판정
cansend can0 277#01
```

## Output Encoder Zero Capture Command

- ID: `0x280 + node_id`
- DLC: `1`
- 의미: 현재 `AS5600` 절대각을 출력축 `0 deg` 기준으로 `FRAM`에 저장하고 boot reference를 다시 잡는다
- 적용 조건:
  - power stage가 `disarmed` 상태일 때만 적용
  - 현재 profile이 `As5600`

Payload:

- `data[0] = output_encoder_type`
  - `1` = `As5600`

동작:

1. 현재 `AS5600` 절대각을 읽는다.
2. 그 각도를 `zero_offset_rad`로 `FRAM`에 저장한다.
3. 출력축 기준과 boot reference를 즉시 다시 잡는다.

예시:

```bash
# 현재 AS5600 각도를 output 0 deg로 저장
cansend can0 287#01
```

## Runtime Diagnostic

- ID: `0x5F0 + node_id`
- DLC: `8`
- 송신 주기: `500 ms`

Payload:

- `data[0] = 0xFB`
- `data[1] = stored output_encoder_type`
- `data[2] = active output_encoder_type`
- `data[3] = default_control_mode`
- `data[4]` bitfield:
  - bit0 = `enable_velocity_mode`
  - bit1 = `enable_output_angle_mode`
  - bit2 = trusted FOC calibration valid
  - bit3 = trusted output calibration valid for current profile
  - bits4..5 = calibration load status (`0` = none, `1` = trusted CRC/commit slot)
- `data[5] = runtime_fault`
- `data[6]` bitfield:
  - bit0 = `need_calibration`
  - bits4..7 = last profile-select result
- `data[7]` bitfield:
  - bit0 = output feedback required
  - bit1 = power stage armed

Profile-select result:

- `0` = `None`
- `1` = `Ok`
- `2` = `RejectedArmed`
- `3` = `As5600ReadFailed`
- `4` = `NotSelectable`
- `5` = `SaveFailed`

Runtime fault:

- `0` = `None`
- `1` = `FollowingError`
- `2` = `FocInitFailed`
- `3` = `As5600ReadFailed`
- `4` = `CalibrationSaveFailed`

예시:

- `FB 02 02 01 1F 00 00 01`
  - stored profile = `TmagLut`
  - active profile = `TmagLut`
  - default control mode = `OutputAngle`
  - velocity mode enabled = `1`
  - angle mode enabled = `1`
  - trusted FOC calibration valid = `1`
  - trusted output calibration valid = `1`
  - calibration load status = `Trusted`
  - runtime fault = `None`
  - calibration blocking = `0`
  - last profile-select result = `None`
  - output feedback required = `1`
  - power stage armed = `0`

- `FB 00 00 02 01 00 00 00`
  - stored profile = `VelocityOnly`
  - active profile = `VelocityOnly`
  - default control mode = `OutputVelocity`
  - velocity mode enabled = `1`
  - angle mode enabled = `0`
  - trusted FOC calibration valid = `0`
  - trusted output calibration valid = `0`
  - calibration load status = `None`
  - runtime fault = `None`
  - calibration blocking = `0`
  - last profile-select result = `None`
  - output feedback required = `0`
  - power stage armed = `0`

## Timeout

첫 유효 제어 명령을 받은 이후 timeout 로직이 활성화된다.

- 현재 제어 모드가 `OutputAngle`이면:
  - timeout 시 현재 출력축 각도를 target으로 고정
- 현재 제어 모드가 `OutputVelocity`이면:
  - timeout 시 목표 속도를 `0 deg/s`로 강제

## Current Profiles

### `VelocityOnly`

- 별도 출력축 절대각 피드백 없음
- 속도제어만 가능
- 각도제어 불가

### `DirectInput`

- `gear_ratio == 1:1` 경로
- 입력축 엔코더 `AS5048A`를 곧 출력축 엔코더로 사용
- 각도제어 가능
- 속도제어 가능
- 별도 출력축 엔코더 없음

### `As5600`

- `AS5600`을 부팅 시 출력축 `0 deg` 기준 정렬과 zero capture에 사용
- 저장된 `AS5600` zero가 없으면, profile 변경 시 현재 `AS5600` 절대각을 첫 `0 deg` 기준으로 저장한다
- 저장된 `AS5600` zero가 이미 있으면 profile 변경만으로 기준점을 덮어쓰지 않는다
- `AS5600` 센서 read가 실패하면 profile 변경은 적용되지 않는다

### `TmagLut`

- `TMAG LUT`를 부팅 시 출력축 `0 deg` 기준 정렬과 zero capture에 사용
- 유효한 `TMAG` calibration이 먼저 저장되어 있어야 한다
