# NoFW 펌웨어 사용자 설명서

## 1. 이 펌웨어는 무엇인가

이 펌웨어는 `STM32F446RE` 기반 BLDC 액추에이터 메인 펌웨어다.

이제 더 이상 그리퍼 전용 `% open` 펌웨어로 보지 않는다.
현재 기준 제어 단위는 다음 두 가지다.

- 출력축 각도: `deg`
- 출력축 속도: `deg/s`

즉 사용자는 이 펌웨어를:

- 일반 회전 액추에이터
- 제한된 각도 범위를 갖는 메커니즘
- 그리퍼처럼 특정 범위만 움직이는 시스템

모두에 사용할 수 있다.

그리퍼처럼 쓰고 싶다면 그리퍼 가동범위를 `output_min_deg`, `output_max_deg`로 맞춰 쓰면 된다.

추가로 기억할 점:

- 실제 명령 좌표계는 항상 출력축 기준이다
- 현재 저장된 `output_min_deg`, `output_max_deg` 바깥으로 angle command를 보내도 내부에서 clamp된다
- 이 clamp 범위는 액추에이터마다 다를 수 있으며 `FRAM`에 저장된 config를 따른다

## 2. 하드웨어 구성

현재 코드가 가정하는 주요 하드웨어:

- MCU: `STM32F446RE`
- 입력축 엔코더: `AS5048A`
- 출력축 엔코더 후보 1: `AS5600`
- 출력축 엔코더 후보 2: `TMAG5170`
- 비휘발성 메모리: `FM25CL64B-G` SPI `FRAM`
- 통신: `CAN`
- 업로드: `ST-Link`

## 3. 프로파일 개념

이 펌웨어는 하나의 메인 펌웨어 안에서 여러 출력축 기준 설정 경로를 지원한다.

중요한 정책은 다음과 같다.

- 출력축 엔코더는 부팅 시 `0 deg` 기준 설정에만 사용한다
- manual zero를 저장할 때도 출력축 엔코더를 사용할 수 있다
- 하지만 런타임 FOC 제어와 CAN angle/velocity status는 입력축 `AS5048A` 멀티턴 추정 기준으로 동작한다

### `VelocityOnly`

- 출력축 절대각 피드백 없음
- 속도제어만 가능
- 각도제어는 할 수 없음

### `DirectInput`

- `gear_ratio == 1:1`인 경우를 위한 모드
- 입력축 엔코더 `AS5048A`를 곧 출력축 엔코더로 사용
- 별도 출력축 엔코더가 없어도 각도제어 가능
- 속도제어도 가능

### `As5600`

- `AS5600`을 부팅 시 출력축 기준각 설정용 센서로 사용
- 저장된 `AS5600` zero가 아직 없더라도, profile 변경 시 센서 read가 성공하면 현재 `AS5600` 절대각을 첫 `0 deg` 기준으로 저장하고 진입한다
- 이미 저장된 `AS5600` zero가 있으면 profile 변경만으로 그 기준을 덮어쓰지 않는다

### `TmagLut`

- `TMAG LUT`를 부팅 시 출력축 기준각 설정용 센서로 사용
- 먼저 `TMAG` 보정이 완료되어 있어야 한다

## 4. 0도 기준은 어떻게 잡히는가

### 출력축 엔코더가 있는 경우

- 저장된 `zero_offset`을 기준으로 `0 deg`를 복원한다
- 부팅할 때 현재 절대각을 다시 읽어 같은 기준점을 맞춘다
- `As5600` profile의 최초 진입 시 저장된 기준점이 없으면, 현재 `AS5600` 절대각이 첫 `0 deg` 기준으로 저장된다
- 이 절차가 끝나면 런타임 제어는 다시 입력축 멀티턴 추정 기준으로 진행된다

### `DirectInput`인 경우

- `AS5048A`가 곧 출력축 절대각 센서이므로 같은 방식으로 `0 deg`를 저장할 수 있다
- 단, 기본적으로는 single-turn absolute 기준이다
- multi-turn 절대 위치를 전원 껐다 켜도 완전히 복원하려면 시스템 travel 조건이 맞아야 한다

### `VelocityOnly`인 경우

- 출력축 절대각 기준이 없다
- 속도만 제어한다

## 5. 가장 많이 쓰는 펌웨어 환경

실사용 기준으로 중요한 것은 두 개다.

- `custom_f446re`
  - 메인 펌웨어
- `tmag_calibration_runner_f446re`
  - `TMAG LUT` calibration 전용 펌웨어

예전 설정용 helper firmware들은 이제 repo에서 제거되었다.
출력 profile 변경은 메인 펌웨어가 CAN으로 직접 처리한다.

## 6. 기본 시작 절차

### 6.1 `can0` 올리기

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
ip -details link show can0
```

### 6.2 메인 펌웨어 업로드

```bash
pio run -e custom_f446re -t upload
```

### 6.2.1 로컬 웹 UI로 테스트하고 싶다면

터미널에서 직접 `cansend`, `candump`를 다루는 대신,
다음 로컬 웹 UI를 사용할 수 있다.

```bash
python3 tools/can_ui/server.py --can-iface can0 --node-id 7 --port 8765
```

브라우저에서:

```text
http://127.0.0.1:8765
```

이 UI는 현재 펌웨어가 CAN으로 제공하는 다음 동작을 한 화면에서 수행한다.

- profile 변경
- power stage `arm/disarm`
- angle command
- velocity command
- `0x407`, `0x417`, `0x5F7` 상태 확인

자세한 범위와 설계는 다음 문서를 본다.

- [can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)

### 6.3 현재 profile 확인

```bash
candump can0,5F7:7FF
```

예시:

```text
5F7#FB02020101010001
```

의미:

- stored profile = `2` = `TmagLut`
- active profile = `2` = `TmagLut`
- default control mode = `OutputAngle`
- calibration blocking = `0`
- data[7] bit0 = output feedback required = `1`
- data[7] bit1 = power stage armed = `0`

### 6.4 CAN node ID 동기화

- 이 펌웨어는 부팅 중 `ActuatorConfig`를 읽을 때, FRAM에 저장된 `can_node_id`가
  현재 펌웨어의 `CAN_NODE_ID`와 다르면 FRAM 값을 펌웨어 값으로 덮어쓴다.
- 즉 현재 빌드에 포함된 `CAN_NODE_ID`가 최종적으로 우선한다.
- 여러 보드에 서로 다른 ID를 쓰려면 보드별로 다른 `CAN_NODE_ID`로 빌드/업로드하면 된다.

즉 이 상태는:

- profile과 calibration은 정상적으로 읽혔고
- 아직 전력단은 올라오지 않았으며
- 실제 구동 전에 사용자가 명시적으로 arm 해야 한다는 뜻이다

### 6.4 가장 안전한 첫 동작 확인

처음에는 큰 이동보다 아래 순서로 확인하는 편이 안전하다.

1. runtime diag가 정상인지 확인한다
2. arm 한다
3. 작은 angle command 하나만 보낸다
4. 상태 프레임을 본다
5. disarm 한다

예시:

```bash
# 1) arm
cansend can0 237#01

# 2) 10.000 deg 명령
cansend can0 207#10270000

# 3) 상태 확인
candump can0,407:7FF can0,417:7FF can0,5F7:7FF

# 4) disarm
cansend can0 237#00
```

## 7. CAN 제어

현재 CAN 명령과 상태는 모두 출력축 단위 `deg`, `deg/s`를 사용한다.
단, 상태값의 계산 기준은 런타임 output encoder가 아니라 입력축 `AS5048A` 멀티턴 추정이다.

중요:

- 메인 펌웨어는 부팅 직후 기본적으로 power stage `disarmed` 상태로 올라온다
- 즉 보드에 전원을 넣었다고 해서 gate와 FOC가 자동으로 켜지지 않는다
- 실제 모터 구동 전에 먼저 arm 명령을 보내야 한다

## 7.1 출력축 각도 명령

- ID: `0x200 + node_id`
- 기본 node `7`이면 `0x207`
- payload: `int32 mdeg`

예시:

- `50.000 deg`
  - raw = `50000`
  - frame = `207#50C30000`

명령:

```bash
cansend can0 207#50C30000
```

현재 각도 상태:

```bash
candump can0,407:7FF
```

이 상태값은 부팅 때 정렬된 `0 deg` 기준을 바탕으로, 런타임에는 입력축 멀티턴 추정으로 계산된다.

중요:

- 현재 출력각이 `output_min_deg .. output_max_deg` 안에 있으면 angle command는 이 범위로 clamp된다
- 현재 출력각이 이미 범위 밖에 있으면, 더 바깥쪽으로 가는 명령은 현재 위치 hold로 제한된다
- 현재 출력각이 이미 범위 밖에 있더라도, 범위 안쪽으로 돌아오는 명령은 허용된다
- 따라서 매우 큰 양수/음수 angle command를 보내더라도 내부 목표는 저장된 travel 범위 또는 현재 위치 보호 조건을 넘지 않는다

## 7.2 출력축 속도 명령

- ID: `0x210 + node_id`
- 기본 node `7`이면 `0x217`
- payload: `int32 mdeg/s`

예시:

- `100.000 deg/s`
  - raw = `100000`
  - frame = `217#A0860100`

명령:

```bash
cansend can0 217#A0860100
```

현재 속도 상태:

```bash
candump can0,417:7FF
```

이 값 역시 런타임 output encoder 값이 아니라 입력축 멀티턴 변화율 기반이다.
또한 velocity 명령은 내부 slew limit를 거쳐 적용되므로, 큰 step 명령을 줘도 실제 속도 요구치는 점진적으로 변한다.

중요:

- velocity mode는 현재 policy상 angle travel edge를 기준으로 별도 braking 하지 않는다
- 즉 velocity mode에서는 사용자가 시스템 조건과 운전 범위를 알고 명령해야 한다

## 7.3 제어 모드 전환

메인 펌웨어는 별도 “angle mode” 버튼 없이, 마지막으로 받은 명령 종류에 따라 자동으로 제어 모드를 바꾼다.

현재 동작 정책은 다음과 같다.

- velocity mode:
  - 출력축 속도 명령을 직접 따른다
  - 내부 slew limit는 적용되지만, output angle travel edge를 기준으로 별도 braking 하지는 않는다
- angle mode:
  - 출력축 각도 목표를 position outer loop로 추종한다
  - configured travel edge 근처에서는 edge-braking cap이 적용된다
  - 그 뒤에도 최종 velocity command는 한 번 더 부드러운 slew limit를 거쳐 급격한 감속/반전을 줄인다
  - 따라서 edge 근처에서도 즉시 hard stop보다는 완만한 감속/정착 쪽에 가깝다

- 각도 명령을 받으면 `OutputAngle`
- 속도 명령을 받으면 `OutputVelocity`

## 7.4 Power Stage Arm / Disarm

- ID: `0x230 + node_id`
- 기본 node `7`이면 `0x237`
- payload:
  - `01` = arm
  - `00` = disarm

예시:

```bash
# arm
cansend can0 237#01

# disarm
cansend can0 237#00
```

설명:

- `arm` 전에는 상태 프레임과 진단 프레임은 볼 수 있어도 실제 전력단은 올라오지 않는다
- `arm` 명령을 받으면 그 시점에 gate enable과 `initFOC()`가 수행된다
- `disarm` 명령을 받으면 전력단을 다시 내린다
- 현재 펌웨어에서는 `arm` 자체가 출력축 기준각을 다시 틀어버리지 않도록 수정되어 있다

## 8. CAN으로 출력 profile 바꾸기

이제 profile 변경을 위해 설정용 펌웨어를 다시 올릴 필요가 없다.

- profile command ID: `0x220 + node_id`
- 기본 node `7`이면 `0x227`
- payload:
  - `00` = `VelocityOnly`
  - `01` = `As5600`
  - `02` = `TmagLut`
  - `03` = `DirectInput`

예시:

### `VelocityOnly`로 변경

```bash
cansend can0 227#00
```

### `As5600`으로 변경

```bash
cansend can0 227#01
```

저장된 `AS5600` zero가 없으면 이 시점의 `AS5600` 절대각을 첫 `0 deg` 기준으로 저장한다.
따라서 실제 기구의 기준 위치에서 이 명령을 보내야 한다.

### `TmagLut`로 변경

```bash
cansend can0 227#02
```

결과 확인:

```bash
candump can0,5F7:7FF
```

실제 검증 예시:

- `0x227#00` 전송 후:
  - `FB 00 00 02 01 00 00 00`
- `0x227#02` 전송 후:
  - `FB 02 02 01 01 01 00 01`

즉 메인 펌웨어가:

- 요청을 받고
- 즉시 적용하고
- `FRAM`에 저장하고
- runtime diag에 반영한다

주의:

- `stored profile`과 `active profile`은 "부팅 시 출력축 기준을 어떤 경로로 맞출지"를 의미한다
- 런타임 FOC 제어와 `0x407/0x417` 상태는 여전히 입력축 `AS5048A` 멀티턴 기준이다

## 9. `TMAG LUT` 사용 절차

### 단계 1. calibration runner 업로드

```bash
pio run -e tmag_calibration_runner_f446re -t upload
```

### 단계 2. calibration 저장 확인

필요하면 `FRAM` 진단으로 저장 여부를 확인한다.

### 단계 3. 메인 펌웨어 다시 업로드

```bash
pio run -e custom_f446re -t upload
```

### 단계 4. profile을 `TmagLut`로 전환

```bash
cansend can0 227#02
```

### 단계 5. runtime diag 확인

```bash
candump can0,5F7:7FF
```

stored/active가 둘 다 `2`면 현재 런타임 피드백이 `TMAG LUT`라는 뜻이다.
정확히는 현재 부팅 기준 설정에 `TMAG LUT` 경로가 선택되어 있다는 뜻이고,
런타임 FOC 제어와 CAN 상태는 여전히 입력축 멀티턴 기준으로 동작한다.

## 10. `DirectInput` 사용 조건

`DirectInput`은 아무 때나 선택되는 게 아니라, 실제로 `gear_ratio == 1:1`인 시스템을 위한 경로다.

이 모드의 의미는:

- 별도 출력축 엔코더가 없어도
- 입력축 엔코더가 곧 출력축 엔코더이므로
- 각도제어와 속도제어를 모두 허용

현재 하드웨어/설정이 `1:1`이 아니면 이 profile은 선택되지 않도록 제한된다.

## 11. Timeout 동작

첫 유효 명령을 받은 뒤 timeout 로직이 활성화된다.

- angle mode:
  - timeout 시 현재 각도 유지
- velocity mode:
  - timeout 시 목표 속도 `0 deg/s`

## 12. 자주 보는 진단 프레임

### `0x5F0 + node_id`

가장 중요한 프레임이다.

다음 내용을 보여 준다.

- 저장된 profile
- 현재 active profile
- 기본 control mode
- velocity/angle enable bit
- calibration blocking 여부

### `0x400 + node_id`

- 현재 출력축 각도
- 단위: `deg`
- payload: `int32 mdeg`

### `0x410 + node_id`

- 현재 출력축 속도
- 단위: `deg/s`
- payload: `int32 mdeg/s`

### `0x420 + node_id`

- 현재 저장/적용된 출력축 travel limit
- payload:
  - `data[0..3] = output_min_deg` as `int32 mdeg`
  - `data[4..7] = output_max_deg` as `int32 mdeg`

기본 node `7`에서는 `0x427`이다.

### `0x430 + node_id`

- 현재 저장/적용된 actuator config 요약
- payload:
  - `data[0..3] = gear_ratio * 1000` as `int32`
  - `data[4] = stored output_encoder_type`
  - `data[5] = default_control_mode`
  - `data[6] bit0 = enable_velocity_mode`
  - `data[6] bit1 = enable_output_angle_mode`

기본 node `7`에서는 `0x437`이다.

## 13. CAN으로 actuator config 바꾸기

설정 변경은 전력단이 내려간 상태에서만 수행해야 한다.
펌웨어도 `armed` 상태에서는 travel limit / gear ratio 설정 프레임을 적용하지 않는다.

권장 공통 절차:

1. `0x237#00`으로 disarm 한다
2. `0x5F7`에서 armed bit가 `0`인지 확인한다
3. 설정 command를 보낸다
4. `0x427` 또는 `0x437`에서 실제 반영 여부를 확인한다

### output_min_deg / output_max_deg

- command ID: `0x240 + node_id`
- 기본 node `7`: `0x247`
- DLC: `8`
- payload:
  - `data[0..3] = output_min_deg` as `int32 mdeg`
  - `data[4..7] = output_max_deg` as `int32 mdeg`

예시:

```bash
# 0 .. 2160 deg
cansend can0 247#0000000080F52000
```

### gear ratio

- command ID: `0x250 + node_id`
- 기본 node `7`: `0x257`
- DLC: `4`
- payload:
  - `data[0..3] = gear_ratio * 1000` as `int32`

예시:

```bash
# 8.000:1
cansend can0 257#401F0000
```

gear ratio 변경은 출력축 좌표계 해석 자체를 바꾼다.
적용 후에는 boot reference를 다시 잡고 current-angle hold 상태로 돌아간다.
`DirectInput` profile은 `gear_ratio == 1.000`일 때만 유효하다.
`TmagLut` profile은 저장된 TMAG calibration의 learned gear ratio와 현재 gear ratio가 다르면 calibration required 상태가 된다.

## 14. 처음 쓸 때 추천 순서

1. `can0`를 `1 Mbps`로 올린다
2. `custom_f446re`를 업로드한다
3. `0x5F7`로 현재 profile, active profile, armed bit를 확인한다
4. `0x427`, `0x437`로 travel limit와 gear ratio를 확인한다
5. 필요하면 `0x227`로 원하는 profile을 바꾼다
6. 먼저 `0x237#01`로 arm 한다
7. 작은 angle command `0x207` 또는 작은 velocity command `0x217`으로 시험한다
8. 시험이 끝나면 `0x237#00`으로 disarm 한다
9. `TMAG`를 쓸 계획이면 calibration runner를 먼저 실행한다

## 15. 현재 벤치에서 확인된 동작 요약

이 항목은 현재 검증된 예시일 뿐이며, 다른 액추에이터에서는 저장된 config에 따라 값이 달라질 수 있다.

현재 벤치에서는 다음이 확인되었다.

- 부팅 직후 power stage는 자동으로 arm되지 않는다
- explicit arm/disarm CAN 경로가 정상 동작한다
- `angle mode`에서 `0 deg` 접근 시 하한 근처에서 정착 가능하다
- 큰 양의 angle command를 보내도 내부 clamp에 의해 저장된 상한에서 멈춘다

현재 테스트 유닛의 최근 관측 예시:

- 하한 근처 최종 정착: 약 `-0.09 deg`
- 큰 양의 angle command 후 상한 근처 정착: 약 `2173.9 deg`

이 값 자체를 다른 시스템의 고정 스펙으로 해석하면 안 된다.
중요한 점은:

- 이 시스템에서 저장된 travel limit가 실제로 적용되고
- arm/disarm과 angle-mode edge 접근이 현재 리비전에서 안정적으로 동작했다는 것이다

## 16. 문제 발생 시 점검

### 각도 명령이 안 먹을 때

- 현재 profile이 angle-capable인지 확인
- `0x5F7`에서 `enable_output_angle_mode = 1`인지 확인
- `need_calibration = 1`인지 확인

### 속도 명령이 안 먹을 때

- `0x5F7`에서 `enable_velocity_mode = 1`인지 확인
- 올바른 ID `0x217`을 쓰고 있는지 확인
- `arm`을 먼저 했는지 확인

### profile 변경이 안 되는 것 같을 때

- `0x227`에 올바른 enum 값을 보냈는지 확인
- `0x5F7`에서 stored/active profile이 바뀌었는지 확인
- `As5600`인데 stored/active가 `1`로 바뀌지 않으면 `AS5600` I2C read 실패를 먼저 의심
- `TmagLut`는 calibration이 없으면 활성화되지 않을 수 있음

### `DirectInput`가 안 되는 것 같을 때

- 시스템이 정말 `gear_ratio == 1:1`인지 확인
- 현재 config가 그 조건으로 저장되어 있는지 확인

### 보드 전원은 켰는데 안 움직일 때

- `0x5F7` 마지막 바이트 bit1이 `0`이면 아직 disarmed 상태다
- 먼저 `0x237#01`을 보내 arm 해야 한다
- arm 후에도 움직이지 않으면 현재 profile과 calibration 상태를 다시 본다

### 큰 각도 명령을 보냈는데 기대한 각도까지 안 가는 것 같을 때

- 현재 시스템은 `output_min_deg`, `output_max_deg` 범위로 angle target을 clamp한다
- 단, 현재 출력각이 이미 범위 밖이면 더 바깥쪽으로 가는 명령은 현재 위치 hold로 제한하고, 안쪽으로 들어오는 명령만 허용한다
- 따라서 명령한 값보다 작은 각도에서 멈추는 것이 정상일 수 있다
- 현재 저장된 travel limit를 먼저 의심해야 한다

## 17. 같이 보면 좋은 문서

- [can_protocol.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_protocol.md)
- [can_arch.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_arch.md)
- [can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)
- [host_controller_integration_guide.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/host_controller_integration_guide.md)
- [tmag_output_encoder_report.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/tmag_output_encoder_report.md)
