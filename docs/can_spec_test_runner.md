# CAN Spec Test Runner

`tools/can_spec_test.py`는 현재 메인 펌웨어의 CAN 온와이어 스펙과 기본 런타임 동작을 검증하는 Python 테스트 러너다.

기본 실행은 모터를 움직이지 않는다.

```bash
python3 tools/can_spec_test.py --iface can0 --node-id 7
```

기본 모드에서 검증하는 항목:

- CAN ID 계산
- `int32` little-endian `0.001` 스케일 인코딩/디코딩
- `candump -L` 프레임 파싱
- runtime diagnostic `0x5F0 + node_id` 디코딩
- limits/config status `0x420/0x430 + node_id` 디코딩
- host-side 입력 검증
- live status frame 수신
- disarm 명령 idempotency
- `output_min_deg/output_max_deg` 설정 roundtrip 후 원복
- `gear_ratio` 설정 roundtrip 후 원복
- 현재 profile 재적용 성공 확인
- invalid profile command 무시 확인

하드웨어 없이 프로토콜 테스트만 실행하려면:

```bash
python3 tools/can_spec_test.py --protocol-only
```

JSON 리포트를 남기려면:

```bash
python3 tools/can_spec_test.py --iface can0 --node-id 7 --report /tmp/nofw_can_spec_report.json
```

## Power Stage Tests

전력단 arm/disarm 전이를 포함하려면 명시적으로 `--allow-arm`을 붙인다.

```bash
python3 tools/can_spec_test.py --iface can0 --node-id 7 --allow-arm
```

이 모드는 다음을 추가 검증한다.

- arm 상태가 runtime diagnostic에 반영되는지
- armed 상태에서 profile/config 변경이 거부되는지

## Motion Tests

실제 작은 각도/속도 명령까지 검증하려면 `--allow-motion`을 붙인다.
이 옵션은 자동으로 `--allow-arm`을 포함한다.

```bash
python3 tools/can_spec_test.py --iface can0 --node-id 7 --allow-motion
```

기본 motion command:

- angle step: `2 deg`
- velocity step: `10 deg/s`
- command hold time: `0.6 s`

필요하면 다음처럼 조정한다.

```bash
python3 tools/can_spec_test.py \
  --iface can0 \
  --node-id 7 \
  --allow-motion \
  --angle-step-deg 1.0 \
  --velocity-step-deg 5.0 \
  --motion-hold-s 0.5
```

주의:

- `--allow-motion`은 실제 모터를 움직인다.
- 테스트 종료 시 스크립트는 `velocity=0`, `disarm`, limits/gear/profile 원복을 시도한다.
- OCP 가능성이 있는 기구에서는 먼저 기본 모드와 `--allow-arm`까지만 실행한다.
