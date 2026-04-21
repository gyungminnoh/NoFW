# NoFW

STM32F446RE 기반의 그리퍼 펌웨어 프로젝트입니다.  
이 저장소는 `SimpleFOC`, `AS5048A` 모터 엔코더, `AS5600` 절대각 센서, `CAN` 통신을 묶어
부팅 시 기준점을 잡고, 외부 CAN 명령으로 그리퍼 개폐율을 제어하는 펌웨어를 포함합니다.

## 구성 요약

- 메인 제품 펌웨어: `src/main.cpp`
- CAN 프로토콜 문서: `docs/can_protocol.md`
- CAN 계층 구조 문서: `docs/can_arch.md`
- 보드/핀/동작 상수: `include/board_config.h`

프로젝트는 크게 아래 계층으로 나뉩니다.

- 센서 계층: `AS5048A` 읽기, `AS5600` 부트 기준점 읽기
- 상태 추정 계층: single-turn 센서를 multi-turn 각도로 변환
- 제어 계층: 목표 위치를 속도 명령으로 변환
- CAN 계층: transport / protocol / service 분리
- 애플리케이션 계층: 부팅, 캘리브레이션, 목표 개폐율 적용

## 주요 파일

- [src/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/main.cpp): 실제 그리퍼 제어 펌웨어 엔트리 포인트
- [include/app.h](/home/gyungminnoh/projects/NoFW/NoFW/include/app.h): 메인 펌웨어용 공용 include 묶음
- [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h): 핀, 전압, 기어비, CAN ID, 토크 제한 등 전체 설정
- [include/as5048a_custom_sensor.h](/home/gyungminnoh/projects/NoFW/NoFW/include/as5048a_custom_sensor.h): AS5048A를 SimpleFOC `Sensor`로 감싼 구현
- [src/as5600_bootstrap.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/as5600_bootstrap.cpp): AS5600 절대각을 여러 번 읽어 안정적인 기준값을 얻는 코드
- [include/multi_turn_estimator.h](/home/gyungminnoh/projects/NoFW/NoFW/include/multi_turn_estimator.h): 각도 unwrap으로 multi-turn 위치를 추정
- [include/position_velocity_controller.h](/home/gyungminnoh/projects/NoFW/NoFW/include/position_velocity_controller.h): 외부 위치 제어기
- [include/gripper_api.h](/home/gyungminnoh/projects/NoFW/NoFW/include/gripper_api.h), [src/gripper_api.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/gripper_api.cpp): open percent와 실제 출력각/모터 multi-turn 좌표 변환
- [include/can_transport.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_transport.h), [src/can_transport.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_transport.cpp): CAN 하드웨어 접근
- [include/can_protocol.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_protocol.h), [src/can_protocol.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_protocol.cpp): CAN 프레임 포맷 정의
- [include/can_service.h](/home/gyungminnoh/projects/NoFW/NoFW/include/can_service.h), [src/can_service.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/can_service.cpp): CAN 명령 반영, timeout 정책, 위치 보고

## 실행 흐름

메인 펌웨어의 부팅 순서는 대략 아래와 같습니다.

1. SPI, CAN, 드라이버, 모터를 초기화합니다.
2. 외장 SPI FRAM에서 모터 FOC 캘리브레이션 정보와 AS5600 기준값을 읽습니다.
3. 필요하면 `initFOC()` 결과를 외장 SPI FRAM에 저장합니다.
4. 현재 모터 각도를 기준으로 multi-turn 추정을 리셋합니다.
5. AS5600 절대각을 읽어 출력축 기준 0점을 정의합니다.
6. 이후 `loop()`에서:
   - `motor.loopFOC()`
   - 센서 업데이트 및 multi-turn 위치 계산
   - CAN 명령 수신
   - 목표 open percent를 목표 출력각으로 변환
   - 위치 오차를 속도 명령으로 변환
   - `motor.move(vel_cmd)` 적용

버튼 길게 누르기로 런타임 캘리브레이션, FRAM 저장값 클리어, 수동 zero 지정 모드도 수행할 수 있습니다.

## 빌드

`platformio.ini`에는 메인 펌웨어와 유지보수용 진단 펌웨어가 함께 들어 있습니다.

- `custom_f446re`: 메인 그리퍼 펌웨어
- `fram_test_f446re`: 외장 FRAM 유지보수용 진단 펌웨어
- `tmag_comm_test_f446re`: TMAG5170 통신 확인용 진단 펌웨어
- `tmag_sensor_test_f446re`: TMAG5170 센서 동작 확인용 진단 펌웨어
- `tmag_xyz_live_test_f446re`: 손으로 축을 돌리며 TMAG5170 XYZ 값을 실시간 확인하는 펌웨어
- `tmag_lut_angle_test_f446re`: AS5600 기준 LUT로 TMAG5170 출력축 각도 추정 성능을 검증하는 펌웨어

예시:

```bash
pio run -e custom_f446re
```

```bash
pio run -e fram_test_f446re
```

```bash
pio run -e tmag_comm_test_f446re
```

```bash
pio run -e tmag_sensor_test_f446re
```

```bash
pio run -e tmag_xyz_live_test_f446re
```

```bash
pio run -e tmag_lut_angle_test_f446re
```

`pio run`만 실행하면 기본값으로 메인 펌웨어만 빌드됩니다. 평소 개발과 배포는 이 경로만 사용하면 됩니다.

## FRAM 진단 펌웨어

외장 `FM25CL64B-G`가 정상 동작하는지 메인 펌웨어와 분리해서 확인하려면
`fram_test_f446re`를 사용하면 됩니다.

이 펌웨어는 제품 기능용이 아니라 아래 상황에서 쓰는 유지보수 도구입니다.

- 새 PCB 조립 직후 FRAM 하드웨어 점검
- SPI 배선 변경 후 회귀 확인
- 저장 관련 이상 증상 발생 시 원인 분리

- 테스트 대상: FRAM 마지막 128바이트 예약 영역
- 확인 항목: 주소 범위 검사, 즉시 write/read 검증, 전원 재인가 후 데이터 유지 검증
- 캘리브레이션 저장 영역과는 분리된 주소를 사용하므로 메인 저장 데이터는 건드리지 않습니다.

권장 순서는 아래와 같습니다.

1. `fram_test_f446re`를 업로드합니다.
2. CAN에서 `0x5A0 + CAN_NODE_ID`, `0x5B0 + CAN_NODE_ID`, `0x5C0 + CAN_NODE_ID`를 확인합니다.
3. 요약 프레임 상태 코드가 `1`이면 전원을 완전히 껐다 켭니다.
4. 다시 확인했을 때 상태 코드가 `2`이면 write/read와 전원 재인가 유지까지 모두 통과한 것입니다.

진단 결과는 CAN 표준 프레임으로 반복 송신합니다.

- CAN ID: `0x5A0 + CAN_NODE_ID`
- 저수준 진단 ID: `0x5B0 + CAN_NODE_ID`
- 상태레지스터 진단 ID: `0x5C0 + CAN_NODE_ID`
- 주기: `500 ms`
- Byte 0: `0xF6` 고정 시그니처
- Byte 1: 상태 코드
- Byte 2: 실패 단계 번호
- Byte 4-5: 실패 주소 (little-endian)

저수준 진단 프레임은 아래 형식입니다.

- Byte 0: `0xF7`
- Byte 1: `WREN` 전 상태 레지스터
- Byte 2: `WREN` 후 상태 레지스터
- Byte 3: 테스트 write 후 상태 레지스터
- Byte 4: 테스트 주소의 원래 값
- Byte 5: 써야 했던 기대값
- Byte 6: write 후 실제 읽힌 값

상태레지스터 진단 프레임은 아래 형식입니다.

- Byte 0: `0xF8`
- Byte 1: `WRSR` 전 BP 비트(`BP1:BP0`)
- Byte 2: `WRSR` 후 BP 비트(`BP1:BP0`)
- Byte 3: 원복 후 BP 비트(`BP1:BP0`)

상태 코드는 아래와 같습니다.

- `0`: 실패
- `1`: 1차 쓰기/읽기 성공, 전원 껐다 켠 뒤 재검증 필요
- `2`: 전원 재인가 후 유지 검증까지 성공

정상 완료 기준:

- `0x5A0 + CAN_NODE_ID` 프레임의 Byte 1이 `2`
- `0x5B0 + CAN_NODE_ID` 프레임에서 기대값과 실제 읽기값이 일치
- `0x5C0 + CAN_NODE_ID` 프레임에서 BP 비트 변경과 원복이 정상

## TMAG5170 통신 확인용 진단 펌웨어

SPI 버스의 `SPI1_nCS_3`에 연결된 `TMAG5170`의 기본 SPI 통신과 레지스터 접근만 확인하려면
`tmag_comm_test_f446re`를 사용하면 됩니다.

이 펌웨어는 아래 상황에서 쓰는 유지보수 도구입니다.

- 새 PCB 조립 직후 TMAG5170 SPI 통신 확인
- SPI 버스 변경 후 CS/배선 회귀 확인
- TMAG5170 응답 이상 시 통신 계층 문제 분리

검사 항목은 아래와 같습니다.

- CRC 비활성화 명령 적용 확인
- `TEST_CONFIG` 레지스터 read/write 확인
- `AFE_STATUS` 레지스터 읽기 확인

진단 결과는 CAN 표준 프레임으로 반복 송신합니다.

- 요약 ID: `0x5D0 + CAN_NODE_ID`
- 설정 ID: `0x5E0 + CAN_NODE_ID`
- 상세 ID: `0x5F0 + CAN_NODE_ID`
- 주기: `500 ms`

요약 프레임 형식:

- Byte 0: `0xD6`
- Byte 1: 상태 코드
- Byte 2: 실패 단계
- Byte 4-5: `OSC_MONITOR` 값

설정 프레임 형식:

- Byte 0: `0xD7`
- Byte 1-2: CRC 비활성화 후 `TEST_CONFIG`
- Byte 3-4: HFOSC 시작 후 `TEST_CONFIG`
- Byte 5-6: 첫 번째 `AFE_STATUS`

상세 프레임 형식:

- Byte 0: `0xD8`
- Byte 1-2: 두 번째 `AFE_STATUS`
- Byte 3-4: CRC 비활성화 후 SPI 상태 비트
- Byte 5-6: HFOSC 시작 후 SPI 상태 비트
- Byte 7: CRC 비활성화 명령 응답의 최하위 바이트

상태 코드는 아래와 같습니다.

- `0`: 실패
- `1`: 통과

## TMAG5170 센서 동작 확인용 진단 펌웨어

TMAG5170이 실제로 자기장 데이터를 내는지 확인하려면 `tmag_sensor_test_f446re`를 사용하면 됩니다.

이 펌웨어는 아래 상황에서 쓰는 유지보수 도구입니다.

- 자석 위치 변화에 따라 TMAG5170 출력이 변하는지 확인
- 실제 측정 레지스터가 읽히는지 확인
- 통신은 되는데 센서 결과가 이상할 때 원인 분리

진단 결과는 CAN 표준 프레임으로 반복 송신합니다.

- 상태 ID: `0x600 + CAN_NODE_ID`
- 축 결과 ID: `0x610 + CAN_NODE_ID`
- 보조 결과 ID: `0x620 + CAN_NODE_ID`
- 주기: `100 ms`

상태 프레임 형식:

- Byte 0: `0xE6`
- Byte 1-2: `CONV_STATUS`
- Byte 3-4: `CONV_STATUS` read 시 SPI 상태 비트

축 결과 프레임 형식:

- Byte 0: `0xE7`
- Byte 1-2: `X_CH_RESULT`
- Byte 3-4: `Y_CH_RESULT`
- Byte 5-6: `Z_CH_RESULT`

보조 결과 프레임 형식:

- Byte 0: `0xE8`
- Byte 1-2: `TEMP_RESULT`
- Byte 3-4: `X_CH_RESULT` read 시 SPI 상태 비트
- Byte 5-6: `Y_CH_RESULT` read 시 SPI 상태 비트
- Byte 7: `Z_CH_RESULT` read 시 SPI 상태 하위 바이트

## TMAG5170 실시간 XYZ 확인 펌웨어

자기장 벡터를 손으로 돌려보며 바로 확인하려면 `tmag_xyz_live_test_f446re`를 사용하면 됩니다.

이 펌웨어는 아래 상황에서 유용합니다.

- 자석을 손으로 움직일 때 `X/Y/Z` 값이 실제로 변하는지 확인
- 센서 방향을 바꾸었을 때 각 축의 부호와 변화 경향 확인
- LUT 캘리브레이션 전에 하드웨어 배치가 대략 맞는지 빠르게 점검

진단 결과는 CAN 표준 프레임으로 반복 송신합니다.

- 상태 ID: `0x600 + CAN_NODE_ID`
- mT ID: `0x610 + CAN_NODE_ID`
- raw ID: `0x620 + CAN_NODE_ID`
- 설정 ID: `0x630 + CAN_NODE_ID`
- X/Y/Z 원시 SPI 프레임 ID: `0x640`, `0x650`, `0x660` + `CAN_NODE_ID`
- 디코드 보조 ID: `0x670 + CAN_NODE_ID`

## TMAG5170 LUT 기반 출력축 각도 검증 펌웨어

출력축 각도를 `TMAG5170` 단독으로 어느 정도 복원할 수 있는지 확인하려면
`tmag_lut_angle_test_f446re`를 사용하면 됩니다.

이 펌웨어는 아래 목적에 맞는 유지보수 도구입니다.

- `AS5600` 기준으로 `TMAG5170 XYZ -> output angle` LUT를 생성
- 혼합된 자계에서도 출력축 각도 추정이 가능한지 검증
- calibration RMS와 validation RMS를 분리해서 확인

동작 개요:

- 부팅 후 잠시 대기한 뒤 모터를 자동으로 약 `2 x 1.15` 출력회전 구간 구동
- 첫 구간에서 LUT 캘리브레이션 수행
- 두 번째 구간에서 LUT 추정각과 `AS5600` 기준각의 오차 누적

진단 결과는 CAN 표준 프레임으로 반복 송신합니다.

- 요약 ID: `0x780 + CAN_NODE_ID`
- 각도 ID: `0x790 + CAN_NODE_ID`
- 통계 ID: `0x7A0 + CAN_NODE_ID`
- 벡터 ID: `0x7B0 + CAN_NODE_ID`
- 진폭 ID: `0x7C0 + CAN_NODE_ID`

핵심 프레임 형식:

- `0xF1`: phase, fit ready, calibration sample 수, LUT valid bin 수
- `0xF2`: 기준각, LUT 추정각, 순간 오차, 선택된 LUT bin
- `0xF3`: calibration RMS, validation RMS, validation MAE
- `0xF4`: 최신 `TMAG X/Y/Z raw`
- `0xF5`: 축별 amplitude와 최대 검증 오차

`AS5600` 기준각에 대해 `TMAG X/Y/Z`를 한 바퀴 구간으로 그래프화하려면 아래 순서로 진행하면 됩니다.

```bash
pio run -e tmag_lut_angle_test_f446re -t upload
candump -L can0 > capture/tmag_lut.log
.venv/bin/python tools/plot_tmag_as5600_turn.py capture/tmag_lut.log --show
```

이 스크립트는 기본적으로 calibration phase에서 첫 번째 360도 구간을 찾아
`capture/tmag_as5600_turn.png`와 `capture/tmag_as5600_turn.csv`를 생성합니다.

## CAN 동작

현재 메인 펌웨어는 다음 정책을 사용합니다.

- 명령 RX: `0..100%` open percent
- 위치 TX: 현재 모터 위치 기반 open percent
- timeout: 일정 시간 유효한 RX가 없으면 현재 위치 유지
- 첫 유효 명령을 받기 전까지는 부팅 기본 목표를 유지

프레임 형식과 자세한 예시는 [docs/can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)를 참고하면 됩니다.

## 개발 메모

- 테스트 중 생성되는 `capture/` 로그와 그래프는 보관 대상이 아니라 필요할 때 다시 생성하는 용도로 봅니다.
