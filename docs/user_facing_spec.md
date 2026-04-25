# NoFW User-Facing Specification

Spec version: `1.1.0`

이 문서는 NoFW 액추에이터 펌웨어의 외부 사용자 계약이다.
상위 제어기, 테스트 장비, 운영자가 의존해도 되는 CAN 프로토콜, 보드 ID, 기본 운용 절차만 정의한다.

내부 구현, FRAM layout, PID 구현, 센서 처리 방식, 파일 구조는 이 문서의 계약 범위가 아니다.

## Stability Policy

이 문서의 내용은 웬만해서는 바꾸지 않는다.

특히 아래 항목은 stable contract로 취급한다.

- CAN bitrate
- frame ID 계산식
- payload byte order
- numeric scale
- enum 값
- board label과 node ID 매핑
- arm/disarm 의미
- status/diagnostic frame 의미
- command timeout 의미

변경이 필요한 경우 원칙은 다음과 같다.

- 기존 의미를 바꾸는 변경은 breaking change다.
- breaking change는 `Spec version` major를 올리고, 가능하면 새 CAN ID 또는 새 enum 값을 추가한다.
- 기존 payload field의 의미를 조용히 바꾸지 않는다.
- reserved field는 기존 값을 깨지 않는 확장에만 사용한다.
- 내부 리팩토링은 이 문서를 바꾸는 이유가 될 수 없다.

## Bus Contract

- Physical bus: CAN
- Bitrate: `1 Mbps`
- Frame type: `11-bit standard ID`
- Endianness: little-endian
- Numeric command/status scale:
  - angle: `int32 mdeg`
  - velocity: `int32 mdeg/s`
  - gear ratio: `int32`, scale `0.001`

Examples:

- `10.000 deg` -> `10000` -> payload `10 27 00 00`
- `-90.000 deg` -> `-90000` -> payload `70 A0 FE FF`
- `50.000:1` gear ratio -> `50000` -> payload `50 C3 00 00`

## Deployed Nodes

These node IDs are part of the user-facing contract for this spec version.

Protocol `1.0.0` is released for steering nodes `1..4` and driving nodes `5..8`.
The current frame family uses `0x10`-spaced function bases, so released node IDs must stay in `1..15`.

| Board | Role | Wheel position | Node ID | Hex | Primary command |
|---|---|---|---:|---:|---|
| `S01` | steering | `FR` | `1` | `0x01` | angle `0x201` |
| `S02` | steering | `BR` | `2` | `0x02` | angle `0x202` |
| `S03` | steering | `FL` | `3` | `0x03` | angle `0x203` |
| `S04` | steering | `BL` | `4` | `0x04` | angle `0x204` |
| `D01` | driving | `FR` | `5` | `0x05` | velocity `0x215` |
| `D02` | driving | `BR` | `6` | `0x06` | velocity `0x216` |
| `D03` | driving | `FL` | `7` | `0x07` | velocity `0x217` |
| `D04` | driving | `BL` | `8` | `0x08` | velocity `0x218` |

Default steering configuration:

- Profile: `As5600`
- Gear ratio: `50.000`
- Travel limits: `-120.000 .. +120.000 deg`
- Control capability: angle and velocity

Default driving configuration:

- Profile: `VelocityOnly`
- Gear ratio: `5.200`
- Control capability: velocity only

## Frame ID Contract

For every frame below, `node_id` is the deployed node ID.

| Direction | Meaning | Frame ID |
|---|---|---:|
| RX | output angle command | `0x200 + node_id` |
| TX | output angle status | `0x400 + node_id` |
| RX | output velocity command | `0x210 + node_id` |
| TX | output velocity status | `0x410 + node_id` |
| TX | actuator travel limits status | `0x420 + node_id` |
| TX | actuator config status | `0x430 + node_id` |
| RX | output profile command | `0x220 + node_id` |
| RX | power-stage command | `0x230 + node_id` |
| RX | travel limits config command | `0x240 + node_id` |
| RX | gear ratio config command | `0x250 + node_id` |
| RX | output encoder direction config | `0x260 + node_id` |
| RX | output encoder direction auto-calibration | `0x270 + node_id` |
| RX | output encoder zero capture | `0x280 + node_id` |
| RX | FOC calibration command | `0x290 + node_id` |
| TX | runtime diagnostic | `0x5F0 + node_id` |

## Enums

### OutputEncoderType

| Value | Name | Meaning |
|---:|---|---|
| `0` | `VelocityOnly` | no output absolute angle feedback; velocity control only |
| `1` | `As5600` | AS5600 is used to restore/capture output zero |
| `2` | `TmagLut` | TMAG LUT is used to restore output zero |
| `3` | `DirectInput` | AS5048A input encoder is treated as output encoder for `1:1` systems |

### ControlMode

| Value | Name |
|---:|---|
| `1` | `OutputAngle` |
| `2` | `OutputVelocity` |

### RuntimeFault

| Value | Name | Meaning |
|---:|---|---|
| `0` | `None` | no active fault |
| `1` | `FollowingError` | commanded motion was not followed for too long |
| `2` | `FocInitFailed` | FOC initialization failed during arm or FOC calibration |
| `3` | `As5600ReadFailed` | AS5600 read failed during required operation |
| `4` | `CalibrationSaveFailed` | calibration could not be saved |

### ProfileSelectResult

| Value | Name |
|---:|---|
| `0` | `None` |
| `1` | `Ok` |
| `2` | `RejectedArmed` |
| `3` | `As5600ReadFailed` |
| `4` | `NotSelectable` |
| `5` | `SaveFailed` |

## Commands

### Output Angle Command

- ID: `0x200 + node_id`
- DLC: `4`
- Payload: `int32 mdeg`
- Meaning: target output angle in degrees

The firmware clamps the target to the configured travel limits.
The host should stream this command periodically until the target is reached.
A single command may time out before the actuator reaches the target.

Example, S01 to `+10.000 deg`:

```bash
cansend can0 201#10270000
```

### Output Velocity Command

- ID: `0x210 + node_id`
- DLC: `4`
- Payload: `int32 mdeg/s`
- Meaning: target output velocity in degrees per second

Example, S01 to `+5.000 deg/s`:

```bash
cansend can0 211#88130000
```

### Power-Stage Command

- ID: `0x230 + node_id`
- DLC: `1`
- Payload:
  - `00` = disarm
  - `01` = arm

Power stage is disarmed after boot.
The actuator must be explicitly armed before motion.
Sending disarm also clears a latched runtime fault when the fault source is gone.

Examples for S01:

```bash
cansend can0 231#01
cansend can0 231#00
```

### FOC Calibration Command

- ID: `0x290 + node_id`
- DLC: `1`
- Payload:
  - `01` = run FOC calibration

This command is accepted only while disarmed.

Behavior:

1. Temporarily enables the power stage.
2. Ignores any stored FOC calibration.
3. Runs motor FOC alignment from `UNKNOWN` sensor direction and unset electrical zero.
4. Stores the new FOC calibration in trusted FRAM calibration storage.
5. Disarms the power stage after the command finishes.

This command updates only the FOC calibration. It does not change output encoder zero, output encoder direction, profile, travel limits, gear ratio, or node ID.

After completion, verify runtime diagnostic:

- `data[4] bit2` trusted FOC calibration valid = `1`
- `data[5]` runtime fault = `None`
- `data[7] bit1` armed = `0`

If output encoder calibration is still missing, `need calibration` may remain `1` even when FOC calibration succeeded.

Examples:

```bash
# S01 FOC calibration
cansend can0 291#01

# S02 FOC calibration
cansend can0 292#01
```

### Output Encoder Zero Capture

- ID: `0x280 + node_id`
- DLC: `1`
- Payload:
  - `01` = `As5600`

Meaning: save the current AS5600 absolute angle as output `0 deg`.
This command is accepted only while disarmed and only in `As5600` profile.

Example for S01:

```bash
cansend can0 281#01
```

### Configuration Commands

Configuration commands are accepted only while disarmed.
After sending a config command, verify the matching status or diagnostic frame.

Travel limits:

- ID: `0x240 + node_id`
- DLC: `8`
- Payload:
  - `data[0..3] = output_min_deg` as `int32 mdeg`
  - `data[4..7] = output_max_deg` as `int32 mdeg`

Gear ratio:

- ID: `0x250 + node_id`
- DLC: `4`
- Payload:
  - `data[0..3] = gear_ratio * 1000` as `int32`

Output profile:

- ID: `0x220 + node_id`
- DLC: `1`
- Payload:
  - `data[0] = OutputEncoderType`

AS5600 direction config:

- ID: `0x260 + node_id`
- DLC: `2`
- Payload:
  - `data[0] = 1`
  - `data[1] = 0` for normal, `1` for inverted

AS5600 direction auto-calibration:

- ID: `0x270 + node_id`
- DLC: `1`
- Payload:
  - `data[0] = 1`

## Status Frames

### Output Angle Status

- ID: `0x400 + node_id`
- DLC: `4`
- Period: `50 ms`
- Payload: current output angle as `int32 mdeg`

### Output Velocity Status

- ID: `0x410 + node_id`
- DLC: `4`
- Period: `50 ms`
- Payload: current output velocity as `int32 mdeg/s`

### Travel Limits Status

- ID: `0x420 + node_id`
- DLC: `8`
- Period: `500 ms`
- Payload:
  - `data[0..3] = output_min_deg` as `int32 mdeg`
  - `data[4..7] = output_max_deg` as `int32 mdeg`

### Actuator Config Status

- ID: `0x430 + node_id`
- DLC: `8`
- Period: `500 ms`
- Payload:
  - `data[0..3] = gear_ratio * 1000` as `int32`
  - `data[4] = stored OutputEncoderType`
  - `data[5] = default ControlMode`
  - `data[6]` bit0 = velocity mode enabled
  - `data[6]` bit1 = output angle mode enabled
  - `data[7] = reserved`

### Runtime Diagnostic

- ID: `0x5F0 + node_id`
- DLC: `8`
- Period: `500 ms`

Payload:

- `data[0] = 0xFB`
- `data[1] = stored OutputEncoderType`
- `data[2] = active OutputEncoderType`
- `data[3] = default ControlMode`
- `data[4]` bitfield:
  - bit0 = velocity mode enabled
  - bit1 = output angle mode enabled
  - bit2 = trusted FOC calibration valid
  - bit3 = trusted output calibration valid for current profile
  - bits4..5 = calibration load status
- `data[5] = RuntimeFault`
- `data[6]` bitfield:
  - bit0 = need calibration
  - bits4..7 = ProfileSelectResult
- `data[7]` bitfield:
  - bit0 = output feedback required
  - bit1 = power stage armed

For normal ready steering operation, the host expects:

- stored profile = `As5600`
- active profile = `As5600`
- trusted FOC calibration valid = `1`
- trusted output calibration valid = `1`
- runtime fault = `None`
- need calibration = `0`

Example S01 ready and disarmed:

```text
5F1#FB0101011F001001
```

## Startup And Calibration Procedure

After firmware upload or board replacement:

1. Bring CAN up at `1 Mbps`.
2. Confirm runtime diagnostic frame `0x5F0 + node_id` is visible.
3. Confirm config status and travel limits match the deployed board.
4. After motor replacement or if the previous motor's FOC calibration may be stored, send FOC calibration command while disarmed.
5. If `need calibration = 1`, send arm once.
6. Wait until runtime diagnostic reports:
   - trusted FOC calibration valid = `1`
   - trusted output calibration valid = `1`
   - need calibration = `0`
   - runtime fault = `None`
7. Disarm.
8. If the mechanical output zero should be the current physical pose, send output encoder zero capture while disarmed.
9. Arm again and test a small angle or velocity command.
10. Disarm after test.

## Command Timeout

The host must treat target commands as a stream, not as one-shot position goals.

After the first valid command, timeout behavior is:

- In angle mode: timeout changes the target to current-angle hold.
- In velocity mode: timeout changes target velocity to `0 deg/s`.

Recommended host behavior:

- Send active motion commands at `20 Hz` or faster.
- Keep streaming the desired target until the status frame confirms arrival.
- Stop streaming and disarm when motion is complete and holding torque is not required.

## Fault Handling

If runtime diagnostic reports `RuntimeFault != None`:

1. Stop sending motion commands.
2. Send disarm.
3. Inspect the reported fault code.
4. Fix the physical or calibration cause before re-arming.
5. Re-arm only after diagnostic reports fault cleared.

Following error means the actuator was commanded to move but did not follow sufficiently.
Do not repeatedly re-arm into a following-error fault without checking wiring, mechanical blockage, motor health, encoder state, and calibration.
