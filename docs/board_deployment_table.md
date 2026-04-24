# 보드별 배포 테이블

이 문서는 현재 계획된 `9`개 보드의 배포 정보를 정리한 운영 문서다.

목적:

- 각 물리 보드에 어떤 펌웨어 설정을 넣어야 하는지 명확히 한다
- 같은 CAN 버스에서 `node_id` 충돌을 방지한다
- 보드 교체나 재배포 시 기준표로 사용한다

현재는 물리 라벨이 아직 확정되지 않았다고 보고, 다음 임시 라벨 체계를 사용한다.

- steering: `S01` ~ `S04`
- driving: `D01` ~ `D04`
- gripper: `G01`

`node_id`는 역할별로 구간을 나눠 배정했다.

- steering: `11` ~ `14`
- driving: `21` ~ `24`
- gripper: `31`

이렇게 하면 `candump`만 봐도 어떤 계열 보드인지 구분하기 쉽다.

## 배포 테이블

| board label | role | planned node_id | profile | control capability | gear ratio | travel limits (deg) | output encoder policy | note |
|---|---|---:|---|---|---|---|---|
| `S01` | steering front-left | `11` | `As5600` | angle + velocity | `50:1` | `-120 ~ 120` | boot zero by `AS5600`, runtime feedback by `AS5048A` multi-turn | steering group |
| `S02` | steering front-right | `12` | `As5600` | angle + velocity | `50:1` | `-120 ~ 120` | boot zero by `AS5600`, runtime feedback by `AS5048A` multi-turn | steering group |
| `S03` | steering rear-left | `13` | `As5600` | angle + velocity | `50:1` | `-120 ~ 120` | boot zero by `AS5600`, runtime feedback by `AS5048A` multi-turn | steering group |
| `S04` | steering rear-right | `14` | `As5600` | angle + velocity | `50:1` | `-120 ~ 120` | boot zero by `AS5600`, runtime feedback by `AS5048A` multi-turn | steering group |
| `D01` | driving front-left | `21` | `VelocityOnly` | velocity only | `78:15` | not used | no output absolute encoder required | driving group |
| `D02` | driving front-right | `22` | `VelocityOnly` | velocity only | `78:15` | not used | no output absolute encoder required | driving group |
| `D03` | driving rear-left | `23` | `VelocityOnly` | velocity only | `78:15` | not used | no output absolute encoder required | driving group |
| `D04` | driving rear-right | `24` | `VelocityOnly` | velocity only | `78:15` | not used | no output absolute encoder required | driving group |
| `G01` | gripper | `31` | `As5600` | angle + velocity | `30:1` | `0 ~ 90` | boot zero by `AS5600`, runtime feedback by `AS5048A` multi-turn | gripper group |

## 적용 규칙

각 보드에 대해 아래 값을 맞춰 배포한다.

- `CAN_NODE_ID`: 위 표의 `planned node_id`
- profile:
  - steering, gripper: `As5600`
  - driving: `VelocityOnly`
- gear ratio:
  - steering: `50.0`
  - driving: `78.0 / 15.0`에 해당하는 비율
  - gripper: `30.0`
- output travel limits:
  - steering: `output_min_deg = -120`, `output_max_deg = 120`
  - driving: not used in `VelocityOnly`
  - gripper: `output_min_deg = 0`, `output_max_deg = 90`

주의:

- 현재 펌웨어는 부팅 시 FRAM의 저장된 `can_node_id`를 현재 빌드의 `CAN_NODE_ID`로 자동 동기화한다
- 따라서 실제 적용 ID는 항상 "이번에 업로드한 펌웨어의 `CAN_NODE_ID`"다
- profile과 gear ratio는 CAN 설정 명령 또는 준비된 절차를 통해 각 보드 용도에 맞게 저장해야 한다

## 권장 배포 순서

1. 대상 보드 라벨을 확인한다.
2. 이 문서에서 해당 보드의 `planned node_id`, profile, gear ratio를 확인한다.
3. [include/board_config.h](/home/gyungminnoh/projects/NoFW/NoFW/include/board_config.h:57)의 `CAN_NODE_ID`를 맞춘다.
4. 메인 펌웨어를 업로드한다.
5. 부팅 후 CAN 상태 프레임 ID가 기대값과 일치하는지 확인한다.
6. 해당 보드의 profile과 gear ratio가 표와 일치하도록 저장한다.
7. 실제 장착 위치 또는 교체 이력을 이 문서에 반영한다.

## 후속 업데이트가 필요한 항목

아래 항목은 실제 시스템이 정해지면 이 문서를 갱신하면 된다.

- 최종 물리 라벨 또는 시리얼 번호
- 실제 장착 위치 이름
- gripper의 실제 가동 범위와 초기 zero 기준 작업 절차
