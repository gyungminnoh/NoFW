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
- 출력 프로파일 변경 RX: `0x220 + node_id`
- power-stage arm/disarm RX: `0x230 + node_id`
- 런타임 진단 TX: `0x5F0 + node_id`

기본 `node_id = 7`이면:

- angle cmd = `0x207`
- angle status = `0x407`
- velocity cmd = `0x217`
- velocity status = `0x417`
- profile cmd = `0x227`
- power-stage cmd = `0x237`
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

예시:

```bash
# arm
cansend can0 237#01

# disarm
cansend can0 237#00
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
- `data[4] = enable_velocity_mode`
- `data[5] = enable_output_angle_mode`
- `data[6] = need_calibration`
- `data[7]` bitfield:
  - bit0 = output feedback required
  - bit1 = power stage armed

예시:

- `FB 02 02 01 01 01 00 01`
  - stored profile = `TmagLut`
  - active profile = `TmagLut`
  - default control mode = `OutputAngle`
  - velocity mode enabled = `1`
  - angle mode enabled = `1`
  - calibration blocking = `0`
  - output feedback required = `1`
  - power stage armed = `0`

- `FB 00 00 02 01 00 00 00`
  - stored profile = `VelocityOnly`
  - active profile = `VelocityOnly`
  - default control mode = `OutputVelocity`
  - velocity mode enabled = `1`
  - angle mode enabled = `0`
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
