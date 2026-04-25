# TMAG5170 출력축 엔코더 사용 방식 보고서

## 1. 목적

이 문서는 `TMAG5170`을 이용해 출력축 각도를 추정하는 방식의 원리, 정확도 검증 방법, 그리고 현재까지의 검증 결과를 정리한 보고서이다.

대상 시스템의 센서 역할은 다음과 같다.

- `AS5048A`: 모터 입력축 각도 센서
- `AS5600`: 출력축 절대각 기준 센서
- `TMAG5170`: 3축 자기장 측정 센서

핵심 목표는 `TMAG5170`의 `X/Y/Z` 자기장 벡터로부터 출력축 절대각을 복원하는 것이다.

현재 해석 기준은 다음과 같다.

- `TMAG5170`은 단순 절대각 센서가 아니라 `3축 자기장 벡터 센서`로 사용한다.
- 출력축 자석 성분뿐 아니라 입력축 관련 고조파 성분도 일부 섞여 들어올 수 있다.
- 그럼에도 불구하고 자기장 벡터 패턴이 반복 가능하다면 소프트웨어 LUT 방식으로 출력축 각도를 재구성할 수 있다.

## 2. 사용 원리

### 2.1 센서 사용 개념

`TMAG5170`은 내부 angle engine을 사용하지 않고, 다음 절차로 출력축 각도를 추정한다.

1. `TMAG5170`에서 현재 시점의 `X/Y/Z` raw 값을 읽는다.
2. 출력축 각도별로 미리 학습해 둔 `LUT`와 현재 벡터를 비교한다.
3. 가장 유사한 LUT bin을 현재 출력축 각도로 선택한다.

즉, 현재 구조는:

- `TMAG5170` = 자기장 측정기
- `MCU` = 각도 추정기

의 역할 분담이다.

### 2.2 LUT 생성 방식

LUT는 캘리브레이션 과정에서 만들어진다.

캘리브레이션 시에는 다음 데이터를 동시에 수집한다.

- `TMAG5170`: `X/Y/Z`
- `AS5600`: 출력축 기준각
- `AS5048A`: 입력축 단일턴 각도

첫 번째 출력축 회전 구간에서:

- 출력축 각도 기준으로 LUT bin을 나눈다.
- 각 bin에 대응하는 `TMAG X/Y/Z` 평균값을 저장한다.
- 각 축의 amplitude를 계산한다.
- 입력축과 출력축 관계를 설명하는 `input_sign`과 `input_phase`를 계산한다.

이때 LUT 한 bin은 아래 정보를 가진다.

- `x`
- `y`
- `z`

현재 공용 구조체는 [include/config/calibration_data.h](/home/gyungminnoh/projects/NoFW/NoFW/include/config/calibration_data.h)에 정의되어 있으며, `TmagCalibrationData` 안에:

- `learned_gear_ratio`
- `input_phase_rad`
- `input_sign`
- `amp_x`, `amp_y`, `amp_z`
- `lut[kTmagLutBins]`

를 저장하도록 되어 있다.

### 2.3 런타임 각도 추정 방식

현재 공용 런타임 추정 코드는 [src/sensors/tmag_lut_estimator.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/tmag_lut_estimator.cpp)에 있다.

동작 순서는 다음과 같다.

1. `TMAG5170`에서 `X/Y/Z` raw 값을 읽는다.
2. `AS5048A`에서 현재 입력축 단일턴 각도를 읽는다.
3. 저장된 감속비를 이용해 가능한 출력축 후보 branch를 생성한다.
4. 각 후보 주변 LUT bin만 비교한다.
5. 점수가 가장 작은 bin을 현재 출력축 각도로 선택한다.

여기서 중요한 점은 LUT 전체 `256`개를 무조건 전수 비교하지 않는다는 것이다.

현재 구현은 입력축 각도와 감속비를 이용해 “기구학적으로 가능한 출력축 후보”를 먼저 좁힌다.

후보 개수는 현재 다음 규칙으로 결정된다.

- `candidate_count = round(abs(gear_ratio))`

즉 감속비가 달라지면 branch 후보 개수도 달라진다.

이 점은 범용 액추에이터 펌웨어 관점에서 매우 중요하다.

### 2.4 점수 계산 방식

각 bin과의 유사도는 `X/Y/Z` 3축 거리로 계산한다.

현재 구현은 축별 amplitude로 정규화한 뒤:

```text
dx = (x_measured - x_lut) / amp_x
dy = (y_measured - y_lut) / amp_y
dz = (z_measured - z_lut) / amp_z

score = dx^2 + dy^2 + dz^2
```

를 사용한다.

즉, 현재 자기장 벡터가 LUT의 어느 출력축 각도 bin과 가장 가까운지를 찾는 방식이다.

### 2.5 왜 입력축 각도를 같이 쓰는가

`TMAG5170` 자기장만으로는 LUT 상에서 비슷한 점이 여러 branch에 존재할 수 있다.
특히 입력축 자계 성분이 섞이면 잘못된 branch를 고를 위험이 있다.

이를 줄이기 위해 `AS5048A` 입력축 단일턴 각도와 감속비를 함께 사용한다.

후보 branch 제한의 목적은:

- 자기장 패턴의 주기적 모호성 축소
- 잘못된 LUT branch로 점프하는 문제 제거

이다.

이 candidate-restricted 방식은 이전의 단순 연속 추적 방식보다 안정적이었다.

## 3. 현재 코드 반영 상태

### 3.1 메인 펌웨어 구조

메인 펌웨어는 이제 출력축 엔코더를 추상화된 형태로 다룬다.

관련 파일:

- 공통 인터페이스:
  [include/sensors/output_encoder.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder.h)
- `AS5600` 구현:
  [include/sensors/output_encoder_as5600.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_as5600.h),
  [src/sensors/output_encoder_as5600.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_as5600.cpp)
- `TMAG LUT` 구현:
  [include/sensors/output_encoder_tmag_lut.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_tmag_lut.h),
  [src/sensors/output_encoder_tmag_lut.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_tmag_lut.cpp)
- 엔코더 선택/관리:
  [include/sensors/output_encoder_manager.h](/home/gyungminnoh/projects/NoFW/NoFW/include/sensors/output_encoder_manager.h),
  [src/sensors/output_encoder_manager.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/sensors/output_encoder_manager.cpp)

현재 메인 펌웨어는 단일 런타임 바이너리 안에서 출력축 프로파일을 전환하는 구조다.

즉 현재 상태는:

- `VelocityOnly`, `DirectInput`, `AS5600`, `TMAG LUT` 프로파일이 모두 메인 펌웨어 안에 공존함
- 프로파일 전환은 별도 설정용 펌웨어가 아니라 CAN 명령과 FRAM 저장값으로 처리함
- `TMAG LUT`를 생성하는 캘리브레이션 경로만 별도 `tmag_calibration_runner_f446re`로 유지함

### 3.2 저장 구조

새 저장 구조는 다음 파일에 있다.

- [include/storage/config_store.h](/home/gyungminnoh/projects/NoFW/NoFW/include/storage/config_store.h)
- [src/storage/config_store.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/storage/config_store.cpp)

저장 항목은 크게 두 묶음이다.

- `ActuatorConfig`
- `CalibrationBundle`

`CalibrationBundle` 안에는:

- `FocCalibrationData`
- `DirectInputCalibrationData`
- `As5600CalibrationData`
- `TmagCalibrationData`

가 들어 있다.

현재 메인 펌웨어는:

- CRC/commit marker가 있는 trusted calibration slot만 사용하고
- 예전 단일 calibration record 형식은 fallback으로 사용하지 않는다
- `gear_ratio == 1:1`이면 `AS5048A`를 출력축 엔코더처럼 사용하는 `DirectInput` 경로도 지원한다

## 4. 정확도 검증 방법

### 4.1 검증 펌웨어

정확도 검증은 메인 펌웨어가 아니라 전용 테스트 펌웨어에서 수행했다.

사용 환경:

- `tmag_lut_angle_test_f446re`

관련 파일:

- [src/tmag_lut_angle_test/main.cpp](/home/gyungminnoh/projects/NoFW/NoFW/src/tmag_lut_angle_test/main.cpp)

### 4.2 검증 절차

검증 절차는 다음과 같다.

1. 부팅 후 일정 시간 대기
2. 모터를 저속으로 자동 회전
3. 첫 번째 출력축 회전 구간에서 LUT 학습
4. 두 번째 출력축 회전 구간에서 LUT 검증
5. 검증 중 각 샘플마다:
   - `AS5600` 출력축 기준각
   - `TMAG LUT` 추정각
   - 두 값의 오차
   를 CAN으로 송신

즉 검증 구조는:

- 학습(reference): `AS5600`
- 추정(estimation): `TMAG5170 + AS5048A + LUT`
- 비교 대상: `AS5600 기준 출력축 각도`

### 4.3 로그 분석 방식

테스트 펌웨어가 송신한 CAN 로그는 다음 스크립트로 분석했다.

- [tools/analyze_tmag_lut_error.py](/home/gyungminnoh/projects/NoFW/NoFW/tools/analyze_tmag_lut_error.py)

분석 지표:

- `RMS error`
- `MAE`
- `max absolute error`
- `95 percentile absolute error`
- `bias`
- `stddev`
- 잔류 고조파(`1x`, `8x`)

검증 로그와 산출물:

- 로그:
  [capture/tmag_lut_candidate_restricted_v2.log](/home/gyungminnoh/projects/NoFW/NoFW/capture/tmag_lut_candidate_restricted_v2.log)
- 그래프:
  [capture/tmag_lut_candidate_restricted_v2_analysis.svg](/home/gyungminnoh/projects/NoFW/NoFW/capture/tmag_lut_candidate_restricted_v2_analysis.svg)
- CSV:
  [capture/tmag_lut_candidate_restricted_v2_analysis.csv](/home/gyungminnoh/projects/NoFW/NoFW/capture/tmag_lut_candidate_restricted_v2_analysis.csv)

## 5. 검증 결과

최종적으로 확인된 주요 검증 수치는 다음과 같다.

- samples: `143`
- RMS error: `0.586844 deg`
- MAE: `0.504336 deg`
- max absolute error: `1.240000 deg`
- p95 absolute error: `1.049000 deg`
- bias: `-0.012028 deg`
- stddev: `0.586721 deg`
- residual harmonic `1x`: `0.092594 deg`
- residual harmonic `8x`: `0.040190 deg`

이 결과는 candidate-restricted LUT 방식 적용 후 얻은 값이다.

즉, 현재 검증 결과 기준으로는:

- 평균 오차가 1도 미만
- 최대 오차도 약 `1.24 deg`
- 큰 branch jump 없이 안정적으로 동작

하는 수준이었다.

## 6. 결과 해석

### 6.1 성능 해석

현재 검증 결과는 `TMAG5170`이 단순 raw angle 센서처럼 동작하는 것이 아니라,
기구와 자계가 완벽하지 않아도 소프트웨어 재구성으로 출력축 절대각을 충분히 복원할 수 있음을 보여준다.

특히 중요한 점은:

- 입력축 고조파가 섞여 있어도
- 입력축 각도와 감속비를 이용해 branch를 제한하면
- 출력축 각도 추정이 매우 안정적이었다

는 점이다.

### 6.2 이전 방식과의 비교

초기에는 LUT 상에서 branch ambiguity가 남아 있어 드문 큰 점프가 발생했다.
이후 입력축 각도 기반 candidate restriction을 넣으면서 이 문제가 크게 줄었다.

실질적인 개선 포인트는 다음이었다.

- 전체 LUT 전수 비교 대신 후보 branch 제한
- `input_sign`, `input_phase`, `gear_ratio`를 같이 사용

즉 현재 정확도 향상의 핵심은 LUT 자체보다도 `branch 선택 방식`에 있다.

## 7. 한계와 주의사항

현재 상태에서 주의할 점은 다음과 같다.

1. 본 정확도 수치는 전용 테스트 펌웨어 기준이다.
   메인 펌웨어에 구조를 이식하는 작업은 진행되었지만, 메인 경로에서 같은 수치가 재확인된 것은 아니다.
2. `TMAG LUT`의 정확도는 감속비에 의존한다.
   감속비가 바뀌면 candidate 수와 branch 해석이 달라지므로 기존 LUT를 그대로 재사용하면 안 된다.
3. `TMAG calibration data` 생성은 의도적으로 별도 `tmag_calibration_runner_f446re`에서 수행한다.
4. 일부 액추에이터는 `AS5600`을 출력축 엔코더로 계속 사용할 수 있고, 일부는 `TMAG LUT`를 출력축 엔코더로 사용할 것이다.
5. `gear_ratio == 1:1`인 액추에이터는 별도 출력축 엔코더 없이도 `DirectInput`으로 각도 제어가 가능하다.

## 8. 결론

현재까지의 검증 결과를 기준으로 하면 `TMAG5170`은 다음 방식으로 출력축 엔코더처럼 사용할 수 있다.

- `TMAG5170`에서 `X/Y/Z` 벡터 측정
- `AS5048A` 입력축 단일턴 각도 사용
- 저장된 감속비, 위상, 부호, LUT 메타데이터 사용
- candidate-restricted LUT matching으로 출력축 각도 추정

검증 기준 성능은:

- RMS 약 `0.59 deg`
- MAE 약 `0.50 deg`
- 최대 절대 오차 약 `1.24 deg`

이다.

따라서 현재 결론은 다음과 같다.

- `TMAG5170`은 소프트웨어 LUT 방식으로 출력축 각도 추정에 사용할 수 있다.
- 정확도는 충분히 실용적일 가능성이 높다.
- 다음 핵심 과제는 “추정 원리 검증”이 아니라 “캘리브레이션 경로와 메인 펌웨어 통합”이다.
