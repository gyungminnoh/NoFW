#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "SimpleFOC.h"

#include "board_config.h"
#include "as5048a_custom_sensor.h"
#include "can_transport.h"
#include "fm25cl64b_fram.h"
#include "tmag5170_spi.h"

extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    while (1) {}
  }

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    while (1) {}
  }
}

namespace {

constexpr uint32_t kCalMagic = 0x43414C32UL;
constexpr uint16_t kCalAddr = 0;

constexpr uint8_t kRegDeviceConfig = 0x00;
constexpr uint8_t kRegSensorConfig = 0x01;
constexpr uint8_t kRegSystemConfig = 0x02;
constexpr uint8_t kRegXResult = 0x09;
constexpr uint8_t kRegYResult = 0x0A;
constexpr uint8_t kRegZResult = 0x0B;
constexpr uint8_t kRegAfeStatus = 0x0D;
constexpr uint8_t kRegSysStatus = 0x0E;

constexpr uint16_t kDeviceConfigActive32x = 0x5020;
constexpr uint16_t kSensorConfigXYZ = 0x01C0;
constexpr uint16_t kSystemConfigDefault = 0x0000;

constexpr uint16_t kSummaryCanId = 0x780 + CAN_NODE_ID;
constexpr uint16_t kAngleCanId = 0x790 + CAN_NODE_ID;
constexpr uint16_t kStatsCanId = 0x7A0 + CAN_NODE_ID;
constexpr uint16_t kVectorCanId = 0x7B0 + CAN_NODE_ID;

constexpr uint32_t kStatusPeriodMs = 100;
constexpr uint32_t kSamplePeriodMs = 10;
constexpr uint32_t kStartDelayMs = 2000;
constexpr float kTestGearRatio = 8.0f;
constexpr int kOutputCandidateCount = 8;
constexpr float kOutputTurnsPerPhase = 1.15f;
constexpr float kMotorVelocityRadPerSec = 4.0f;
constexpr float kMotorTargetToleranceRad = 0.05f;
constexpr size_t kMaxCalSamples = 1800;
constexpr size_t kLutBins = 256;
constexpr int kCandidateWindowBins = 6;

struct CalData {
  uint32_t magic;
  int8_t sensor_dir;
  float zero_elec;
  float as5600_zero_ref;
};

struct RawSample {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  float input = 0.0f;
  float ref = 0.0f;
};

struct AxisStats {
  int16_t min_v = INT16_MAX;
  int16_t max_v = INT16_MIN;

  void add(int16_t v) {
    if (v < min_v) min_v = v;
    if (v > max_v) max_v = v;
  }

  float midpoint() const {
    return 0.5f * (static_cast<float>(min_v) + static_cast<float>(max_v));
  }

  float amplitude() const {
    return 0.5f * (static_cast<float>(max_v) - static_cast<float>(min_v));
  }
};

struct LutBin {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  bool valid = false;
};

enum class Phase : uint8_t {
  BootDelay = 0,
  Calibrating = 1,
  Validating = 2,
  Done = 3,
  Error = 4,
};

struct LutFit {
  float mid_x = 0.0f;
  float mid_y = 0.0f;
  float mid_z = 0.0f;
  float amp_x = 1.0f;
  float amp_y = 1.0f;
  float amp_z = 1.0f;
  float input_phase = 0.0f;
  int8_t input_sign = 1;
  int16_t valid_bins = 0;
  float cal_rms = 1e9f;
  float val_err2 = 0.0f;
  float val_abs_sum = 0.0f;
  float val_max_abs = 0.0f;
  size_t val_count = 0;
  bool valid = false;
};

AS5048A_CustomSensor g_input_sensor(PIN_AS5048_CS, SPI);
BLDCMotor g_motor(POLE_PAIRS);
BLDCDriver3PWM g_driver(PIN_PWM_A, PIN_PWM_B, PIN_PWM_C);

Phase g_phase = Phase::BootDelay;
bool g_wire_inited = false;
bool g_as5600_ok = false;
bool g_motor_ready = false;
float g_last_as5600_raw_rad = 0.0f;
float g_as5600_unwrapped_rad = 0.0f;
float g_as5600_origin = 0.0f;
float g_last_motor_raw_rad = 0.0f;
float g_motor_unwrapped_rad = 0.0f;
float g_motor_target_rad = 0.0f;
float g_last_ref_angle = 0.0f;
float g_last_est_angle = 0.0f;
float g_last_err = 0.0f;
uint16_t g_last_as5600_raw = 0;
uint16_t g_last_afe_status = 0;
uint16_t g_last_sys_status = 0;
uint32_t g_boot_ms = 0;
uint8_t g_failure_code = 0;
int16_t g_last_x = 0;
int16_t g_last_y = 0;
int16_t g_last_z = 0;
int16_t g_last_best_bin = -1;

RawSample g_cal_samples[kMaxCalSamples] = {};
size_t g_cal_count = 0;
LutBin g_lut[kLutBins] = {};
int32_t g_lut_sum_x[kLutBins] = {};
int32_t g_lut_sum_y[kLutBins] = {};
int32_t g_lut_sum_z[kLutBins] = {};
uint16_t g_lut_count[kLutBins] = {};
LutFit g_fit = {};

bool loadCalibration(CalData& out) {
  if (!FM25CL64B::readObject(kCalAddr, out)) return false;
  if (out.magic != kCalMagic) return false;
  if (out.sensor_dir != 1 && out.sensor_dir != -1) return false;
  return true;
}

void deselectAllSpiSlaves() {
  pinMode(PIN_AS5048_CS, OUTPUT);
  digitalWrite(PIN_AS5048_CS, HIGH);
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);
  pinMode(PIN_TMAG5170_CS, OUTPUT);
  digitalWrite(PIN_TMAG5170_CS, HIGH);
}

void ensureAs5600WireInit() {
  if (g_wire_inited) return;
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();
  Wire.setClock(100000);
  g_wire_inited = true;
}

bool readAs5600Raw(uint16_t& out_raw) {
  ensureAs5600WireInit();

  Wire.beginTransmission(AS5600_I2C_ADDR);
  Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom((int)AS5600_I2C_ADDR, 2) != 2) {
    return false;
  }

  const uint8_t hi = Wire.read();
  const uint8_t lo = Wire.read();
  out_raw = (((uint16_t)hi << 8) | lo) & 0x0FFFu;
  return true;
}

float wrapPmPi(float a) {
  while (a > PI) a -= 2.0f * PI;
  while (a < -PI) a += 2.0f * PI;
  return a;
}

float wrap0To2Pi(float a) {
  while (a >= 2.0f * PI) a -= 2.0f * PI;
  while (a < 0.0f) a += 2.0f * PI;
  return a;
}

int16_t radToCdeg(float rad) {
  const float deg = rad * (18000.0f / PI);
  if (deg > 32767.0f) return 32767;
  if (deg < -32768.0f) return -32768;
  return static_cast<int16_t>(deg);
}

float cdegToRad(int16_t cdeg) {
  return static_cast<float>(cdeg) * (PI / 18000.0f);
}

int wrapBin(int idx) {
  while (idx < 0) idx += static_cast<int>(kLutBins);
  while (idx >= static_cast<int>(kLutBins)) idx -= static_cast<int>(kLutBins);
  return idx;
}

int circularBinDistance(int a, int b) {
  int d = abs(a - b);
  const int bins = static_cast<int>(kLutBins);
  if (d > (bins / 2)) d = bins - d;
  return d;
}

float binToAngle(int bin) {
  return ((static_cast<float>(bin) + 0.5f) * (2.0f * PI)) / static_cast<float>(kLutBins);
}

int angleToBin(float angle_rad) {
  const float wrapped = wrap0To2Pi(angle_rad);
  int idx = static_cast<int>(floorf((wrapped * static_cast<float>(kLutBins)) / (2.0f * PI)));
  if (idx < 0) idx = 0;
  if (idx >= static_cast<int>(kLutBins)) idx = static_cast<int>(kLutBins) - 1;
  return idx;
}

bool updateAs5600() {
  uint16_t raw = 0;
  if (!readAs5600Raw(raw)) {
    g_as5600_ok = false;
    return false;
  }

  const float rad = (static_cast<float>(raw) * (2.0f * PI)) / 4096.0f;
  if (!g_as5600_ok) {
    g_last_as5600_raw_rad = rad;
    g_as5600_unwrapped_rad = 0.0f;
    g_as5600_origin = 0.0f;
    g_as5600_ok = true;
  } else {
    g_as5600_unwrapped_rad += wrapPmPi(rad - g_last_as5600_raw_rad);
    g_last_as5600_raw_rad = rad;
  }

  g_last_as5600_raw = raw;
  g_last_ref_angle = g_as5600_unwrapped_rad - g_as5600_origin;
  return true;
}

void updateMotorUnwrapped() {
  g_input_sensor.update();
  const float raw = g_input_sensor.getAngle();
  if (!g_motor_ready) {
    g_last_motor_raw_rad = raw;
    g_motor_unwrapped_rad = 0.0f;
    g_motor_target_rad = 2.0f * kOutputTurnsPerPhase * kTestGearRatio * 2.0f * PI;
    g_motor_ready = true;
    return;
  }
  g_motor_unwrapped_rad += wrapPmPi(raw - g_last_motor_raw_rad);
  g_last_motor_raw_rad = raw;
}

float phaseBoundaryRad() {
  return kOutputTurnsPerPhase * kTestGearRatio * 2.0f * PI;
}

bool readTmagXYZ(int16_t& x_raw, int16_t& y_raw, int16_t& z_raw) {
  const auto x = TMAG5170::readRegister(kRegXResult);
  const auto y = TMAG5170::readRegister(kRegYResult);
  const auto z = TMAG5170::readRegister(kRegZResult);
  g_last_afe_status = TMAG5170::readRegister(kRegAfeStatus).data;
  g_last_sys_status = TMAG5170::readRegister(kRegSysStatus).data;
  x_raw = static_cast<int16_t>(x.data);
  y_raw = static_cast<int16_t>(y.data);
  z_raw = static_cast<int16_t>(z.data);
  return true;
}

float scoreLutBin(int16_t x_raw, int16_t y_raw, int16_t z_raw, int bin) {
  const float dx_scale = 1.0f / g_fit.amp_x;
  const float dy_scale = 1.0f / g_fit.amp_y;
  const float dz_scale = 1.0f / g_fit.amp_z;
  const float dx = (static_cast<float>(x_raw) - static_cast<float>(g_lut[bin].x)) * dx_scale;
  const float dy = (static_cast<float>(y_raw) - static_cast<float>(g_lut[bin].y)) * dy_scale;
  const float dz = (static_cast<float>(z_raw) - static_cast<float>(g_lut[bin].z)) * dz_scale;
  return dx * dx + dy * dy + dz * dz;
}

float estimateInputPhase(int sign) {
  float phase_sin = 0.0f;
  float phase_cos = 0.0f;
  for (size_t i = 0; i < g_cal_count; ++i) {
    const float ref_wrapped = wrap0To2Pi(g_cal_samples[i].ref);
    const float phase = wrap0To2Pi(g_cal_samples[i].input - (sign * kTestGearRatio * ref_wrapped));
    phase_sin += sinf(phase);
    phase_cos += cosf(phase);
  }

  float phase = atan2f(phase_sin, phase_cos);
  if (phase < 0.0f) phase += 2.0f * PI;
  return phase;
}

int candidateBestBin(int16_t x_raw, int16_t y_raw, int16_t z_raw, float input_single_turn_rad, int sign, float phase) {
  int best_bin = 0;
  float best_score = 1e30f;
  bool visited[kLutBins] = {};

  for (int candidate = 0; candidate < kOutputCandidateCount; ++candidate) {
    const float candidate_angle =
        wrap0To2Pi((sign * (input_single_turn_rad - phase + (2.0f * PI * candidate))) / kTestGearRatio);
    const int center_bin = angleToBin(candidate_angle);

    for (int delta = -kCandidateWindowBins; delta <= kCandidateWindowBins; ++delta) {
      const int bin = wrapBin(center_bin + delta);
      if (visited[bin]) continue;
      visited[bin] = true;

      const float score = scoreLutBin(x_raw, y_raw, z_raw, bin);
      if (score < best_score) {
        best_score = score;
        best_bin = bin;
      }
    }
  }

  return best_bin;
}

bool finalizeLut() {
  if (g_cal_count < 80) return false;

  AxisStats sx;
  AxisStats sy;
  AxisStats sz;
  for (size_t i = 0; i < g_cal_count; ++i) {
    sx.add(g_cal_samples[i].x);
    sy.add(g_cal_samples[i].y);
    sz.add(g_cal_samples[i].z);
  }

  g_fit.mid_x = sx.midpoint();
  g_fit.mid_y = sy.midpoint();
  g_fit.mid_z = sz.midpoint();
  g_fit.amp_x = sx.amplitude();
  g_fit.amp_y = sy.amplitude();
  g_fit.amp_z = sz.amplitude();
  if (g_fit.amp_x < 16.0f || g_fit.amp_y < 16.0f || g_fit.amp_z < 16.0f) return false;

  memset(g_lut_sum_x, 0, sizeof(g_lut_sum_x));
  memset(g_lut_sum_y, 0, sizeof(g_lut_sum_y));
  memset(g_lut_sum_z, 0, sizeof(g_lut_sum_z));
  memset(g_lut_count, 0, sizeof(g_lut_count));
  memset(g_lut, 0, sizeof(g_lut));

  for (size_t i = 0; i < g_cal_count; ++i) {
    const int bin = angleToBin(g_cal_samples[i].ref);
    g_lut_sum_x[bin] += g_cal_samples[i].x;
    g_lut_sum_y[bin] += g_cal_samples[i].y;
    g_lut_sum_z[bin] += g_cal_samples[i].z;
    ++g_lut_count[bin];
  }

  g_fit.valid_bins = 0;
  for (size_t i = 0; i < kLutBins; ++i) {
    if (g_lut_count[i] == 0) continue;
    g_lut[i].x = static_cast<int16_t>(lroundf(static_cast<float>(g_lut_sum_x[i]) / g_lut_count[i]));
    g_lut[i].y = static_cast<int16_t>(lroundf(static_cast<float>(g_lut_sum_y[i]) / g_lut_count[i]));
    g_lut[i].z = static_cast<int16_t>(lroundf(static_cast<float>(g_lut_sum_z[i]) / g_lut_count[i]));
    g_lut[i].valid = true;
    ++g_fit.valid_bins;
  }

  if (g_fit.valid_bins < 64) return false;

  for (size_t i = 0; i < kLutBins; ++i) {
    if (g_lut[i].valid) continue;

    int prev = -1;
    int next = -1;
    for (size_t step = 1; step < kLutBins; ++step) {
      const int idx = wrapBin(static_cast<int>(i) - static_cast<int>(step));
      if (g_lut[idx].valid) {
        prev = idx;
        break;
      }
    }
    for (size_t step = 1; step < kLutBins; ++step) {
      const int idx = wrapBin(static_cast<int>(i) + static_cast<int>(step));
      if (g_lut[idx].valid) {
        next = idx;
        break;
      }
    }

    if (prev < 0 && next < 0) return false;
    if (prev < 0) prev = next;
    if (next < 0) next = prev;

    const int dist_prev = circularBinDistance(static_cast<int>(i), prev);
    const int dist_next = circularBinDistance(static_cast<int>(i), next);
    const int span = dist_prev + dist_next;

    if (span <= 0) {
      g_lut[i] = g_lut[prev];
    } else {
      const float t = static_cast<float>(dist_prev) / static_cast<float>(span);
      g_lut[i].x = static_cast<int16_t>(lroundf(
          static_cast<float>(g_lut[prev].x) * (1.0f - t) + static_cast<float>(g_lut[next].x) * t));
      g_lut[i].y = static_cast<int16_t>(lroundf(
          static_cast<float>(g_lut[prev].y) * (1.0f - t) + static_cast<float>(g_lut[next].y) * t));
      g_lut[i].z = static_cast<int16_t>(lroundf(
          static_cast<float>(g_lut[prev].z) * (1.0f - t) + static_cast<float>(g_lut[next].z) * t));
      g_lut[i].valid = true;
    }
  }

  float best_sign_err2 = 1e30f;
  int best_sign = 1;
  float best_phase = 0.0f;
  for (int sign : {1, -1}) {
    const float phase = estimateInputPhase(sign);
    float err2 = 0.0f;
    for (size_t i = 0; i < g_cal_count; ++i) {
      const int best_bin = candidateBestBin(
          g_cal_samples[i].x,
          g_cal_samples[i].y,
          g_cal_samples[i].z,
          g_cal_samples[i].input,
          sign,
          phase);
      const float est = binToAngle(best_bin);
      const float ref = wrap0To2Pi(g_cal_samples[i].ref);
      const float err = wrapPmPi(est - ref);
      err2 += err * err;
    }
    if (err2 < best_sign_err2) {
      best_sign_err2 = err2;
      best_sign = sign;
      best_phase = phase;
    }
  }

  g_fit.input_sign = static_cast<int8_t>(best_sign);
  g_fit.input_phase = best_phase;
  g_fit.cal_rms = sqrtf(best_sign_err2 / static_cast<float>(g_cal_count));
  g_fit.valid = true;
  return true;
}

bool evalLutAngle(int16_t x_raw, int16_t y_raw, int16_t z_raw, float input_single_turn_rad, float& out_angle) {
  if (!g_fit.valid) return false;

  const int best_bin =
      candidateBestBin(x_raw, y_raw, z_raw, input_single_turn_rad, g_fit.input_sign, g_fit.input_phase);

  g_last_best_bin = best_bin;
  out_angle = wrapPmPi(binToAngle(best_bin));
  return true;
}

void sendFrames() {
  const float ref_display = wrapPmPi(g_last_ref_angle);
  const float est_display = g_fit.valid ? wrapPmPi(g_last_est_angle) : 0.0f;
  const int16_t ref_cdeg = radToCdeg(ref_display);
  const int16_t est_cdeg = g_fit.valid ? radToCdeg(est_display) : 0;
  const int16_t err_cdeg = g_fit.valid ? radToCdeg(g_last_err) : 0;
  const int16_t cal_rms_cdeg = g_fit.valid ? radToCdeg(g_fit.cal_rms) : 0;
  const int16_t val_rms_cdeg =
      (g_fit.val_count > 0) ? radToCdeg(sqrtf(g_fit.val_err2 / static_cast<float>(g_fit.val_count))) : 0;
  const int16_t val_mae_cdeg =
      (g_fit.val_count > 0) ? radToCdeg(g_fit.val_abs_sum / static_cast<float>(g_fit.val_count)) : 0;
  const int16_t val_max_cdeg = g_fit.valid ? radToCdeg(g_fit.val_max_abs) : 0;

  {
    uint8_t data[8] = {0};
    data[0] = 0xF1;
    data[1] = static_cast<uint8_t>(g_phase);
    data[2] = g_fit.valid ? 1 : 0;
    data[3] = static_cast<uint8_t>(g_cal_count & 0xFFu);
    data[4] = static_cast<uint8_t>((g_cal_count >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(g_fit.valid_bins & 0xFFu);
    data[6] = static_cast<uint8_t>((g_fit.valid_bins >> 8) & 0xFFu);
    data[7] = (g_phase == Phase::Error) ? g_failure_code : static_cast<uint8_t>(g_fit.val_count & 0xFFu);
    CanTransport::sendStd(kSummaryCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    data[0] = 0xF2;
    data[1] = static_cast<uint8_t>(ref_cdeg & 0xFFu);
    data[2] = static_cast<uint8_t>((ref_cdeg >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(est_cdeg & 0xFFu);
    data[4] = static_cast<uint8_t>((est_cdeg >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(err_cdeg & 0xFFu);
    data[6] = static_cast<uint8_t>((err_cdeg >> 8) & 0xFFu);
    data[7] = (g_last_best_bin < 0) ? 0xFFu : static_cast<uint8_t>(g_last_best_bin);
    CanTransport::sendStd(kAngleCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    data[0] = 0xF3;
    data[1] = static_cast<uint8_t>(cal_rms_cdeg & 0xFFu);
    data[2] = static_cast<uint8_t>((cal_rms_cdeg >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(val_rms_cdeg & 0xFFu);
    data[4] = static_cast<uint8_t>((val_rms_cdeg >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(val_mae_cdeg & 0xFFu);
    data[6] = static_cast<uint8_t>((val_mae_cdeg >> 8) & 0xFFu);
    data[7] = static_cast<uint8_t>(g_fit.val_count & 0xFFu);
    CanTransport::sendStd(kStatsCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    data[0] = 0xF4;
    data[1] = static_cast<uint8_t>(g_last_x & 0xFFu);
    data[2] = static_cast<uint8_t>((g_last_x >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(g_last_y & 0xFFu);
    data[4] = static_cast<uint8_t>((g_last_y >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(g_last_z & 0xFFu);
    data[6] = static_cast<uint8_t>((g_last_z >> 8) & 0xFFu);
    data[7] = static_cast<uint8_t>(g_last_afe_status & 0xFFu);
    CanTransport::sendStd(kVectorCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    const int16_t amp_x = static_cast<int16_t>(lroundf(g_fit.amp_x));
    const int16_t amp_y = static_cast<int16_t>(lroundf(g_fit.amp_y));
    const int16_t amp_z = static_cast<int16_t>(lroundf(g_fit.amp_z));
    data[0] = 0xF5;
    data[1] = static_cast<uint8_t>(amp_x & 0xFFu);
    data[2] = static_cast<uint8_t>((amp_x >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(amp_y & 0xFFu);
    data[4] = static_cast<uint8_t>((amp_y >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(amp_z & 0xFFu);
    data[6] = static_cast<uint8_t>((amp_z >> 8) & 0xFFu);
    data[7] = static_cast<uint8_t>(val_max_cdeg & 0xFFu);
    CanTransport::sendStd(kVectorCanId + 0x10u, data, 8);
  }
}

void initMotionSystem() {
  pinMode(PIN_EN_GATE, OUTPUT);
  pinMode(PIN_M_PWM, OUTPUT);
  pinMode(PIN_M_OC, OUTPUT);
  pinMode(PIN_OC_GAIN, OUTPUT);

  digitalWrite(PIN_M_PWM, HIGH);
  digitalWrite(PIN_M_OC, HIGH);
  digitalWrite(PIN_OC_GAIN, HIGH);
  digitalWrite(PIN_EN_GATE, LOW);

  deselectAllSpiSlaves();
  SPI.begin();
  FM25CL64B::begin();
  g_input_sensor.init();
  g_input_sensor.update();

  g_driver.voltage_power_supply = BUS_VOLTAGE;
  g_driver.voltage_limit = VOLTAGE_LIMIT;
  g_driver.init();

  g_motor.linkDriver(&g_driver);
  g_motor.linkSensor(&g_input_sensor);
  g_motor.controller = MotionControlType::velocity;
  g_motor.torque_controller = TorqueControlType::voltage;
  g_motor.voltage_sensor_align = ALIGN_VOLTAGE;
  g_motor.voltage_limit = (TORQUE_LIMIT_VOLTS < VOLTAGE_LIMIT) ? TORQUE_LIMIT_VOLTS : VOLTAGE_LIMIT;
  g_motor.PID_velocity.P = 0.1f;
  g_motor.PID_velocity.I = 0.2f;
  g_motor.PID_velocity.D = 0.0f;
  g_motor.LPF_velocity.Tf = 0.007f;

  CalData cal = {};
  if (loadCalibration(cal)) {
    g_motor.sensor_direction = static_cast<Direction>(cal.sensor_dir);
    g_motor.zero_electric_angle = cal.zero_elec;
  }

  digitalWrite(PIN_EN_GATE, HIGH);
  delay(10);
  g_motor.init();
  if (!g_motor.initFOC()) {
    g_failure_code = 1;
    g_phase = Phase::Error;
    return;
  }

  updateMotorUnwrapped();
}

void initTmag() {
  TMAG5170::begin();
  TMAG5170::disableCrc();
  TMAG5170::writeRegister(kRegDeviceConfig, 0x0000);
  TMAG5170::writeRegister(kRegSensorConfig, kSensorConfigXYZ);
  TMAG5170::writeRegister(kRegSystemConfig, kSystemConfigDefault);
  TMAG5170::writeRegister(kRegDeviceConfig, kDeviceConfigActive32x);
}

} // namespace

void setup() {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  ensureAs5600WireInit();
  updateAs5600();
  initMotionSystem();
  initTmag();
  CanTransport::begin1Mbps();
  g_boot_ms = millis();
}

void loop() {
  static uint32_t last_status_ms = 0;
  static uint32_t last_sample_ms = 0;

  updateMotorUnwrapped();
  g_motor.loopFOC();

  const float boundary = phaseBoundaryRad();
  if (g_phase == Phase::Error) {
    g_motor.move(0.0f);
  } else if (g_phase == Phase::BootDelay) {
    g_motor.move(0.0f);
    if ((uint32_t)(millis() - g_boot_ms) >= kStartDelayMs) {
      g_phase = Phase::Calibrating;
      g_as5600_origin = g_as5600_unwrapped_rad;
    }
  } else if (g_phase == Phase::Calibrating || g_phase == Phase::Validating) {
    const float remaining = g_motor_target_rad - g_motor_unwrapped_rad;
    float target_vel = (remaining >= 0.0f) ? kMotorVelocityRadPerSec : -kMotorVelocityRadPerSec;
    if (fabsf(remaining) <= kMotorTargetToleranceRad) {
      g_phase = Phase::Done;
      target_vel = 0.0f;
    }
    g_motor.move(target_vel);
  } else {
    g_motor.move(0.0f);
  }

  const uint32_t now = millis();
  if ((uint32_t)(now - last_sample_ms) >= kSamplePeriodMs) {
    last_sample_ms = now;

    if (!updateAs5600()) {
      g_failure_code = 2;
      g_phase = Phase::Error;
    } else {
      readTmagXYZ(g_last_x, g_last_y, g_last_z);

      if (g_phase == Phase::Calibrating) {
        if (g_cal_count < kMaxCalSamples) {
            g_cal_samples[g_cal_count].x = g_last_x;
            g_cal_samples[g_cal_count].y = g_last_y;
            g_cal_samples[g_cal_count].z = g_last_z;
            g_cal_samples[g_cal_count].input = g_last_motor_raw_rad;
            g_cal_samples[g_cal_count].ref = g_last_ref_angle;
            ++g_cal_count;
        }
        if (g_motor_unwrapped_rad >= boundary) {
          if (!finalizeLut()) {
            g_failure_code = 3;
            g_phase = Phase::Error;
          } else {
            g_phase = Phase::Validating;
            g_last_best_bin = -1;
            if (evalLutAngle(g_last_x, g_last_y, g_last_z, g_last_motor_raw_rad, g_last_est_angle)) {
              g_last_err = wrapPmPi(g_last_est_angle - g_last_ref_angle);
            } else {
              g_last_est_angle = 0.0f;
              g_last_err = 0.0f;
            }
          }
        }
      } else if (g_phase == Phase::Validating) {
        if (evalLutAngle(g_last_x, g_last_y, g_last_z, g_last_motor_raw_rad, g_last_est_angle)) {
          g_last_err = wrapPmPi(g_last_est_angle - g_last_ref_angle);
          g_fit.val_err2 += g_last_err * g_last_err;
          g_fit.val_abs_sum += fabsf(g_last_err);
          if (fabsf(g_last_err) > g_fit.val_max_abs) g_fit.val_max_abs = fabsf(g_last_err);
          ++g_fit.val_count;
        }
      }
    }
  }

  if ((uint32_t)(now - last_status_ms) >= kStatusPeriodMs) {
    last_status_ms = now;
    sendFrames();
  }
}
