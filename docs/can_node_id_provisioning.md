# CAN Node ID 배포 절차

이 문서는 여러 보드에 서로 다른 `CAN node_id`를 넣어 같은 CAN 버스에 연결할 때의
권장 배포 절차를 정리한다.

현재 펌웨어 정책:

- `node_id`는 런타임 CAN 명령으로 바꾸지 않는다
- 빌드 env가 주입한 `BUILD_CAN_NODE_ID`가 최종 값이다
- 부팅 시 FRAM에 저장된 `can_node_id`가 env 값과 다르면 env 값으로 덮어쓴다

즉 보드별 `node_id`를 나누려면 보드별로 다른 PlatformIO env로 빌드/업로드하면 된다.

현재 표준 env:

- steering
  - `S01`: `custom_f446re_s01` -> node `1`
  - `S02`: `custom_f446re_s02` -> node `2`
  - `S03`: `custom_f446re_s03` -> node `3`
  - `S04`: `custom_f446re_s04` -> node `4`
- driving
  - `D01`: `custom_f446re_d01` -> node `5`
  - `D02`: `custom_f446re_d02` -> node `6`
  - `D03`: `custom_f446re_d03` -> node `7`
  - `D04`: `custom_f446re_d04` -> node `8`

주의:

- user-facing spec `1.0.0`은 steering node `1..4`와 driving node `5..8`을 release한다.
- 현재 frame family는 function base가 `0x10` 간격이므로 같은 bus의 release node ID는 `1..15` 범위 안에서 관리한다.

현재 계획된 `9`개 보드의 실제 배포 표는
[board_deployment_table.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/board_deployment_table.md)에 정리한다.

## 1. 배포 전 준비

보드별 할당표를 먼저 정한다.

예시:

| board label | planned node_id | note |
|---|---:|---|
| `A01` | `7` | left joint |
| `A02` | `8` | right joint |
| `A03` | `9` | wrist |

권장:

- 보드 PCB 또는 하우징에 물리 라벨을 붙인다
- 라벨과 `node_id`를 문서에 같이 적는다
- 같은 버스 안에서는 `node_id`를 중복해서 쓰지 않는다

## 2. 보드별 빌드 값 설정

`node_id`는 `platformio.ini`의 보드별 env가 단일 원본이다.

예:

```ini
[env:custom_f446re_d03]
extends = env:custom_f446re_base
build_flags =
  ${env:custom_f446re_base.build_flags}
  -D BUILD_CAN_NODE_ID=7
```

## 3. 업로드

메인 펌웨어 업로드:

```bash
pio run -e custom_f446re_d03 -t upload
```

중요:

- 이 업로드가 끝나면 부팅 과정에서 FRAM 안의 저장된 `can_node_id`도 같은 env 값으로 자동 정리된다
- 즉 과거에 다른 ID가 저장되어 있었더라도, 현재 업로드한 env 값이 최종적으로 적용된다

## 4. 업로드 후 검증

CAN 인터페이스를 `1 Mbps`로 올린 뒤 상태 프레임 ID를 본다.

예:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
candump can0
```

`node_id = 8`이면 최소한 다음 프레임들이 보여야 한다.

- angle status: `0x408`
- velocity status: `0x418`
- runtime diag: `0x5F8`
- actuator limits status: `0x428`
- actuator config status: `0x438`

`node_id = 7`이면 각각:

- `0x407`
- `0x417`
- `0x5F7`
- `0x427`
- `0x437`

## 5. 권장 작업 순서

보드를 여러 대 배포할 때는 이 순서를 반복한다.

1. 대상 보드 라벨을 확인한다.
2. 할당표에서 해당 보드의 `node_id`를 확인한다.
3. 대상 보드 env를 선택한다.
4. 펌웨어를 빌드하고 업로드한다.
5. `candump`로 기대한 상태 프레임 ID가 나오는지 확인한다.
6. 보드 라벨과 검증 결과를 배포 기록에 남긴다.

## 6. 실수 방지 규칙

- 같은 버스에서 두 보드에 같은 `node_id`를 배포하지 않는다
- 업로드 후에는 반드시 CAN 상태 프레임으로 실제 적용 값을 확인한다
- 보드 교체나 재업로드 후에도 이전 FRAM 값에 의존하지 않는다
- "지금 연결된 보드가 어떤 ID인지"는 항상 실제 수신된 CAN frame ID로 확인한다
