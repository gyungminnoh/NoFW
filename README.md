# NoFW

STM32F446RE 기반의 actuator-generic BLDC 펌웨어입니다. 이 저장소는
`SimpleFOC`, `AS5048A` 입력축 엔코더, 선택 가능한 출력축 엔코더
(`AS5600`, `TMAG5170 LUT`, `DirectInput`), 외장 FRAM, CAN 프로토콜을 묶어
출력축 기준 angle / velocity 제어를 제공합니다.

현재 설계 기준은 제품별 percentage API가 아니라 actuator 공통 단위입니다.

- 출력각: `deg`
- 출력속도: `deg/s`
- CAN wire payload: `mdeg`, `mdeg/s`
- runtime config / calibration 저장소: `FM25CL64B-G` SPI FRAM

## Active Firmware

지원하는 active PlatformIO build target은 아래입니다.

- `custom_f446re_s01`
- `custom_f446re_s02`
- `custom_f446re_s03`
- `custom_f446re_s04`
- `custom_f446re_d01`
- `custom_f446re_d02`
- `custom_f446re_d03`
- `custom_f446re_d04`
- `tmag_calibration_runner_f446re`

`custom_f446re_*` 계열은 메인 actuator firmware입니다.
`tmag_calibration_runner_f446re`는 `TmagLut` runtime profile에 필요한 LUT
calibration을 생성하고 FRAM에 저장하는 전용 펌웨어입니다.

과거 FRAM/TMAG 단품 진단 펌웨어와 일회성 test firmware는 active workflow에서
제거되었습니다. 하드웨어 bring-up용 임시 진단이 다시 필요하면 현재
`can_transport`, `fm25cl64b_fram`, `tmag5170_spi`, `ConfigStore`를 기준으로 새
maintenance tool을 별도 목적에 맞게 추가하세요.

## Main Layout

- [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp): 메인 펌웨어 entrypoint
- [include/app.h](/home/gyungminnoh/projects/NoFW/NoFW/include/app.h): 메인 펌웨어 공용 include 묶음
- [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h): 핀, 전압, 기본 gear/profile, PID, CAN node build default
- [include/actuator_api.h](/home/gyungminnoh/projects/NoFW/NoFW/include/actuator_api.h): actuator angle/velocity/limit 변환 API
- [include/can_protocol.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_protocol.h), [src/can_protocol.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_protocol.cpp): CAN frame encode/decode
- [include/can_service.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_service.h), [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp): CAN command/status service
- [include/storage/config_store.h](/home/gyungminnoh/projects/NoFW/NoFW/include/storage/config_store.h), [src/storage/config_store.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/storage/config_store.cpp): FRAM-backed config/calibration store
- [include/sensors/output_encoder_manager.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_manager.h): runtime output encoder selection
- [src/tmag_calibration_runner/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/tmag_calibration_runner/main.cpp): TMAG LUT calibration firmware

관련 문서:

- [agents.md](/home/gyungminnoh/projects/NoFW/NoFW/agents.md): future agent 운영 규칙
- [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md): CAN protocol
- [docs/firmware_user_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/firmware_user_guide.md): firmware/user workflow
- [docs/board_deployment_table.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/board_deployment_table.md): board deployment mapping
- [docs/tmag_output_encoder_report.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/tmag_output_encoder_report.md): TMAG LUT design notes

## Runtime Profiles

`ActuatorConfig.output_encoder_type`는 runtime output encoder profile을 선택합니다.

- `VelocityOnly`: 별도 출력축 절대 엔코더 없이 velocity mode만 사용
- `DirectInput`: `gear_ratio == 1:1`에서 `AS5048A` 입력축 엔코더를 출력축
  각도 센서로 직접 사용
- `As5600`: `AS5600`을 runtime 출력축 엔코더로 사용
- `TmagLut`: 저장된 `TMAG5170` LUT calibration을 runtime 출력축 엔코더로 사용

`TmagLut` runtime은 valid TMAG calibration이 필요합니다. Calibration flow는
`AS5600`을 reference encoder로 사용하지만, calibration 성공만으로 runtime
profile을 자동 승격하지 않습니다. Profile 변경은 지원되는 config path를 통해
명시적으로 수행해야 합니다.

## Control And CAN Behavior

메인 firmware는 command-driven control mode를 사용합니다.

- angle command 수신: output-angle control
- velocity command 수신: output-velocity control
- angle command timeout: current-angle hold
- velocity command timeout: zero velocity command

대표 CAN frame family:

- output angle command/status: `0x200 + node_id`, `0x400 + node_id`
- output velocity command/status: `0x210 + node_id`, `0x410 + node_id`
- actuator limits/status/config/diagnostic frame은
  [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)를 기준으로 유지합니다.

## Build

기본 빌드는 `custom_f446re_s01`입니다.

```bash
pio run
```

대표 build check:

```bash
pio run -e custom_f446re_s01
pio run -e custom_f446re_d01
pio run -e tmag_calibration_runner_f446re
```

보드별 upload 예시:

```bash
pio run -e custom_f446re_s01 -t upload
```

TMAG LUT calibration firmware upload 예시:

```bash
pio run -e tmag_calibration_runner_f446re -t upload
```

## Verification

Protocol-only regression:

```bash
python3 tools/can_spec_test.py --protocol-only
```

CAN hardware가 연결되어 있고 motion이 필요 없는 상태 확인:

```bash
python3 tools/can_spec_test.py --iface can0 --node-id 1
```

`--allow-arm` 또는 `--allow-motion`은 현재 세션에서 arming/motion이 안전하다고
명시적으로 확인된 경우에만 사용하세요.

CAN UI smoke test:

```bash
python3 tools/can_ui/smoke_test.py
```

## Current Board Defaults

Steering firmware:

- `S01..S04`: node id `1..4`
- 기본 profile은 build/source default와 FRAM config에 따라 결정
- `S01`, `S04`: `BUILD_MOTOR_TO_OUTPUT_SIGN=-1`

Driving firmware:

- `D01..D04`: node id `5..8`
- `VelocityOnly`
- `21` pole pairs
- gear ratio `5.2`
- FOC align voltage `5.0V`
- torque limit override `30.0V`
- velocity PI: `P=0.18`, `I=7.0`, `D=0.0`, `LPF=0.01`

FRAM에 valid config가 있으면 runtime은 저장된 config를 우선 사용합니다. 펌웨어
build default는 config가 없거나 invalid일 때의 seed 값으로 취급하세요.

## FRAM Storage Policy

Actuator config는 현재 store version의 `ActuatorConfig`만 읽습니다. 예전 config
layout은 migration하지 않고, 읽기 실패 시 현재 firmware build default로 새로
저장합니다.

Calibration bundle은 FRAM의 두 fixed slot에 저장됩니다.

- 각 slot은 `magic`, `version`, `sequence`, `crc32`, commit marker, payload를 포함
- 저장 시 payload를 먼저 쓰고 commit marker를 마지막에 기록
- 읽을 때 header, commit marker, CRC, payload sanity check가 모두 통과한 최신
  sequence만 사용
- 예전 단일 calibration record는 fallback으로 읽지 않음

따라서 이 정책이 적용된 firmware를 처음 올린 보드는 기존 FRAM의 예전
calibration 값을 무시할 수 있습니다. 필요한 경우 첫 arm/calibration에서 새
trusted slot을 생성해야 합니다.

## Repository Hygiene

Tracked source/docs에는 active firmware, maintained tools, 유지되는 Markdown
문서만 남기는 것을 원칙으로 합니다.

- raw experiment CSV/SVG/PNG/log/generated JSON은 active docs에 보관하지 않음
- local capture와 build/cache 산출물은 untracked로 유지
- 작업 기록은 `agents.md`에 append하지 않고 필요 시 `docs/worklog/YYYY-MM-DD.md`
  사용
