# 수동 CAN 테스트 체크리스트

이 문서는 사용자가 터미널에서 `cansend`, `candump`를 직접 사용해
프로파일 변경부터 실제 모터 구동까지 수동으로 확인할 때 쓰는 체크리스트다.

만약 직접 hex payload를 다루는 과정이 불편하면, 먼저 다음 문서를 보고
로컬 웹 UI를 실행한 뒤 같은 테스트를 UI에서 수행해도 된다.

- [can_test_web_ui_mvp.md](/home/gyungminnoh/projects/NoFW/NoFW/docs/can_test_web_ui_mvp.md)

목표는 다음 세 가지다.

- 현재 보드가 정상적으로 `CAN`에 응답하는지 확인
- profile 변경, arm/disarm, angle/velocity 명령이 의도대로 동작하는지 확인
- 모터의 실제 움직임을 눈으로 보면서 방향, 정지, clamp 동작을 확인

이 문서는 현재 기본 `node_id = 7` 기준으로 작성되어 있다.

## 1. 시작 전 안전 원칙

- 처음에는 항상 작은 명령부터 시작한다
- 큰 angle/velocity 명령은 작은 명령으로 방향과 반응을 확인한 뒤에만 보낸다
- 테스트가 끝나면 항상 `disarm` 한다
- 이상한 큰 튐, 예상 밖의 방향, 과전류 느낌이 보이면 즉시 `disarm` 한다

즉시 정지 명령:

```bash
cansend can0 237#00
```

## 2. 준비

### 2.1 `can0` 확인

```bash
ip -details link show can0
```

정상 기대:

- `state UP`
- `bitrate 1000000`

필요하면 다시 올린다.

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
```

### 2.2 런타임 진단 프레임 확인

```bash
candump can0,5F7:7FF
```

정상 기대:

- `0x5F7` 프레임이 반복해서 보인다
- 예: `FB02020101010001`

마지막 바이트 판독:

- bit0 = output feedback required
- bit1 = power stage armed

즉:

- `...01`이면 `disarmed`
- `...03`이면 `armed`

## 3. 프로파일 변경 확인

profile command:

- `0x227#00` = `VelocityOnly`
- `0x227#01` = `As5600`
- `0x227#02` = `TmagLut`
- `0x227#03` = `DirectInput`

### 3.1 `VelocityOnly`

```bash
cansend can0 227#00
candump can0,5F7:7FF
```

기대:

- stored profile = `0`
- active profile = `0`
- `enable_output_angle_mode = 0`
- `enable_velocity_mode = 1`

판정:

- 이 상태에서는 속도제어만 되는 것이 정상이다

### 3.2 `As5600`

```bash
cansend can0 227#01
candump can0,5F7:7FF
```

기대:

- stored/active profile이 `1`로 보인다
- angle mode와 velocity mode가 둘 다 가능해야 한다
- 저장된 `AS5600` zero가 없던 보드라면, 이 명령을 보낸 순간의 `AS5600` 절대각이 첫 `0 deg` 기준으로 저장된다
- stored/active가 `1`로 바뀌지 않으면 `AS5600` 센서 read 실패를 먼저 의심한다

### 3.3 `TmagLut`

```bash
cansend can0 227#02
candump can0,5F7:7FF
```

기대:

- stored/active profile이 `2`로 보인다
- calibration이 준비되어 있어야 활성화될 수 있다

### 3.4 `DirectInput`

```bash
cansend can0 227#03
candump can0,5F7:7FF
```

기대:

- 실제 `gear_ratio == 1:1`인 경우에만 의미가 있다
- 현재 시스템이 `1:1`이 아니면 선택되지 않을 수 있다

## 4. Arm / Disarm 확인

### 4.1 Arm

```bash
cansend can0 237#01
candump can0,5F7:7FF
```

기대:

- 마지막 바이트가 `...03`으로 보인다
- 즉 `power stage armed bit = 1`

눈으로 볼 점:

- 출력축 절대 기준을 읽는 profile에서는 부팅 직후 초기 angle target이 `0 deg`이므로, 별도 target을 보내지 않고 arm하면 저장된 원점으로 이동하려고 한다
- 현재 위치를 유지한 채 arm하려면 arm 전에 현재 출력각을 확인하고 그 값을 angle target으로 먼저 보내야 한다

### 4.2 Disarm

```bash
cansend can0 237#00
candump can0,5F7:7FF
```

기대:

- 마지막 바이트가 `...01`로 돌아온다

눈으로 볼 점:

- 모터 구동이 즉시 내려가야 한다

## 5. 가장 안전한 첫 구동 테스트

추천 순서:

1. 원하는 profile 선택
2. `arm`
3. 작은 angle command 한 번
4. 상태 프레임 확인
5. `disarm`

예시:

```bash
cansend can0 237#01
cansend can0 207#10270000
candump can0,407:7FF can0,417:7FF can0,5F7:7FF
cansend can0 237#00
```

여기서 `207#10270000`은 `10.000 deg`다.

판정:

- 모터가 작은 각도로 움직이면 정상
- 움직임 방향이 예상과 맞아야 한다
- `0x407` 각도 상태가 같은 방향으로 변해야 한다

## 6. 각도제어 테스트

## 6.1 작은 양의 각도 명령

```bash
cansend can0 237#01
cansend can0 207#10270000
candump can0,407:7FF
```

기대:

- 모터가 작은 양의 방향으로 움직인다
- `0x407` 값이 증가한다

## 6.2 0도로 복귀

```bash
cansend can0 207#00000000
candump can0,407:7FF
```

기대:

- 모터가 `0 deg` 방향으로 돌아온다
- 하한 근처에서는 약간의 overshoot 뒤 정착할 수 있다

## 6.3 큰 양의 angle command로 상한 clamp 보기

```bash
cansend can0 207#404B4C00
candump can0,407:7FF can0,417:7FF
```

여기서 `207#404B4C00`은 `5000.000 deg`다.

기대:

- 실제로는 내부에서 저장된 `output_max_deg`로 clamp된다
- 따라서 명령값 `5000 deg`까지 가지 않는 것이 정상일 수 있다
- 상한 근처에서 감속 후 정착해야 한다

판정:

- 큰 양의 angle command를 보냈는데 중간에서 멈췄더라도,
  저장된 travel limit라면 정상이다

## 7. 속도제어 테스트

속도제어는 `VelocityOnly`, `As5600`, `TmagLut`, `DirectInput` 중
현재 profile이 속도제어를 허용하면 사용할 수 있다.

## 7.1 작은 양의 속도 명령

```bash
cansend can0 237#01
cansend can0 217#10270000
candump can0,417:7FF
```

여기서 `217#10270000`은 `10.000 deg/s`다.

기대:

- 모터가 천천히 양의 방향으로 회전한다
- `0x417` 값이 양수 쪽으로 움직인다

## 7.2 정지 명령

```bash
cansend can0 217#00000000
candump can0,417:7FF
```

기대:

- 속도가 0 쪽으로 내려온다

중요:

- velocity mode는 현재 정책상 angle edge 기준 braking을 따로 하지 않는다
- 따라서 속도제어 테스트는 항상 작은 명령부터 시작하는 편이 안전하다

## 8. 프로파일별 빠른 판정 기준

### `VelocityOnly`

정상:

- velocity command는 먹는다
- angle command는 안 먹거나 무시된다

### `As5600`

정상:

- arm 후 angle/velocity 둘 다 동작한다
- 부팅 기준 `0 deg` 정렬이 일관되어야 한다

### `TmagLut`

정상:

- calibration이 준비되어 있으면 활성화된다
- angle/velocity 둘 다 동작한다

비정상 의심:

- profile 요청 후에도 stored/active가 `2`로 바뀌지 않음
- `need_calibration = 1`

### `DirectInput`

정상:

- `gear_ratio == 1:1` 시스템에서 angle/velocity 둘 다 가능

비정상 의심:

- 현재 시스템이 `1:1`이 아닌데 이 profile을 기대하고 있음

## 9. 눈으로 볼 때 확인할 핵심 항목

- arm 했을 때 큰 튐이 없는가
- angle command 부호와 실제 회전 방향이 맞는가
- velocity command 부호와 실제 회전 방향이 맞는가
- 큰 angle command를 보내면 저장된 travel limit에서 멈추는가
- disarm 했을 때 전력단이 확실히 내려가는가

## 10. Actuator Config 변경 확인 (disarmed only)

설정 변경은 반드시 disarm 상태에서만 수행한다.

### 10.1 현재 limit / gear ratio 상태 보기

```bash
candump can0,427:7FF,437:7FF
```

기대:

- `0x427`에서 `output_min_deg/output_max_deg`가 보인다
- `0x437`에서 gear ratio가 보인다

### 10.2 travel limit 변경

예시 (`0 .. 2160 deg`):

```bash
cansend can0 237#00
cansend can0 247#0000000080F52000
candump can0,427:7FF
```

기대:

- disarm 이후에도 링크는 유지된다
- `0x427` payload가 설정값과 일치한다

### 10.3 gear ratio 변경

예시 (`8.000`):

```bash
cansend can0 237#00
cansend can0 257#401F0000
candump can0,437:7FF
```

기대:

- `0x437`의 gear ratio가 `8.000`으로 보인다
- 적용 직후 출력각 기준이 재초기화될 수 있다

## 11. 이상 징후와 즉시 대응

### arm 하자마자 이상하게 움직임

즉시:

```bash
cansend can0 237#00
```

그다음 확인:

- 현재 profile이 무엇인지
- `0x5F7`의 armed bit가 어떻게 바뀌는지
- boot 기준이 예상과 다른지

### 각도 명령이 안 먹음

확인:

- 현재 profile이 angle-capable인지
- `enable_output_angle_mode = 1`인지
- `need_calibration = 0`인지
- arm 했는지

### 속도 명령이 안 먹음

확인:

- `enable_velocity_mode = 1`인지
- arm 했는지
- `0x217`에 올바른 payload를 보냈는지

### 큰 각도 명령을 보냈는데 기대보다 덜 움직임

가능한 정상 원인:

- `output_min_deg/output_max_deg` clamp에 걸린 것

즉:

- 저장된 travel limit를 먼저 의심해야 한다

## 12. 테스트 종료

테스트가 끝나면 항상:

```bash
cansend can0 237#00
```

그리고 마지막으로:

```bash
candump can0,5F7:7FF
```

를 봐서 다시 `disarmed` 상태인지 확인한다.
