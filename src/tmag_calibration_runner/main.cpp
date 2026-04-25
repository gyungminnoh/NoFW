#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "SimpleFOC.h"

#include "board_config.h"
#include "as5048a_custom_sensor.h"
#include "can_transport.h"
#include "config/actuator_defaults.h"
#include "config/calibration_constants.h"
#include "fm25cl64b_fram.h"
#include "sensors/tmag_calibration_builder.h"
#include "storage/config_store.h"
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

constexpr uint32_t kStatusPeriodMs = 100;
constexpr uint32_t kSamplePeriodMs = 10;
constexpr uint32_t kStartDelayMs = 2000;
// Current TMAG geometry still matches the validated 8:1 actuator test setup.
constexpr float kTmagCalibrationGearRatio = 8.0f;
constexpr float kOutputTurnsPerPhase = 1.15f;
constexpr float kMotorVelocityRadPerSec = 4.0f;
constexpr float kMotorTargetToleranceRad = 0.05f;

enum class Phase : uint8_t {
  BootDelay = 0,
  Calibrating = 1,
  Validating = 2,
  Done = 3,
  Error = 4,
};

struct ValidationStats {
  float err2_sum = 0.0f;
  float abs_sum = 0.0f;
  float max_abs = 0.0f;
  uint32_t count = 0;
};

AS5048A_CustomSensor g_input_sensor(PIN_AS5048_CS, SPI);
BLDCMotor g_motor(POLE_PAIRS);
BLDCDriver3PWM g_driver(PIN_PWM_A, PIN_PWM_B, PIN_PWM_C);
TmagCalibrationBuilder g_builder = {};
ActuatorConfig g_actuator_config = {};
ConfigStore::CalibrationBundle g_calibration_bundle = {};
TmagCalibrationData g_tmag_calibration = {};
TmagCalibrationMetrics g_metrics = {};
ValidationStats g_validation = {};

Phase g_phase = Phase::BootDelay;
bool g_wire_inited = false;
bool g_as5600_ok = false;
bool g_motor_ready = false;
bool g_saved = false;
float g_last_as5600_raw_rad = 0.0f;
float g_as5600_unwrapped_rad = 0.0f;
float g_as5600_origin_rad = 0.0f;
float g_last_motor_raw_rad = 0.0f;
float g_motor_unwrapped_rad = 0.0f;
float g_motor_target_rad = 0.0f;
float g_last_ref_angle_rad = 0.0f;
float g_last_est_angle_rad = 0.0f;
float g_last_err_rad = 0.0f;
uint16_t g_last_afe_status = 0;
uint16_t g_last_sys_status = 0;
int16_t g_last_x = 0;
int16_t g_last_y = 0;
int16_t g_last_z = 0;
int g_last_best_bin = -1;
uint8_t g_failure_code = 0;
uint32_t g_boot_ms = 0;

float wrapPmPi(float angle_rad) {
  while (angle_rad > PI) angle_rad -= 2.0f * PI;
  while (angle_rad < -PI) angle_rad += 2.0f * PI;
  return angle_rad;
}

int16_t radToCdeg(float rad) {
  const float deg = rad * (18000.0f / PI);
  if (deg > 32767.0f) return 32767;
  if (deg < -32768.0f) return -32768;
  return static_cast<int16_t>(deg);
}

uint16_t summaryCanId() {
  return 0x780 + g_actuator_config.can_node_id;
}

uint16_t angleCanId() {
  return 0x790 + g_actuator_config.can_node_id;
}

uint16_t statsCanId() {
  return 0x7A0 + g_actuator_config.can_node_id;
}

uint16_t vectorCanId() {
  return 0x7B0 + g_actuator_config.can_node_id;
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

bool updateAs5600Reference() {
  uint16_t raw = 0;
  if (!readAs5600Raw(raw)) {
    g_as5600_ok = false;
    return false;
  }

  const float rad = (static_cast<float>(raw) * (2.0f * PI)) / 4096.0f;
  if (!g_as5600_ok) {
    g_last_as5600_raw_rad = rad;
    g_as5600_unwrapped_rad = 0.0f;
    g_as5600_origin_rad = 0.0f;
    g_as5600_ok = true;
  } else {
    g_as5600_unwrapped_rad += wrapPmPi(rad - g_last_as5600_raw_rad);
    g_last_as5600_raw_rad = rad;
  }

  g_last_ref_angle_rad = g_as5600_unwrapped_rad - g_as5600_origin_rad;
  return true;
}

void updateMotorUnwrapped() {
  g_input_sensor.update();
  const float raw = g_input_sensor.getAngle();
  if (!g_motor_ready) {
    g_last_motor_raw_rad = raw;
    g_motor_unwrapped_rad = 0.0f;
    g_motor_target_rad = 2.0f * kOutputTurnsPerPhase * kTmagCalibrationGearRatio * 2.0f * PI;
    g_motor_ready = true;
    return;
  }

  g_motor_unwrapped_rad += wrapPmPi(raw - g_last_motor_raw_rad);
  g_last_motor_raw_rad = raw;
}

float phaseBoundaryOutputRad() {
  return kOutputTurnsPerPhase * 2.0f * PI;
}

void initTmag() {
  TMAG5170::begin();
  TMAG5170::disableCrc();
  TMAG5170::writeRegister(kRegDeviceConfig, 0x0000);
  TMAG5170::writeRegister(kRegSensorConfig, kSensorConfigXYZ);
  TMAG5170::writeRegister(kRegSystemConfig, kSystemConfigDefault);
  TMAG5170::writeRegister(kRegDeviceConfig, kDeviceConfigActive32x);
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

void saveCalibrationResult() {
  if (g_saved || !g_tmag_calibration.valid) {
    return;
  }

  if (g_validation.count > 0) {
    g_tmag_calibration.validation_rms_rad =
        sqrtf(g_validation.err2_sum / static_cast<float>(g_validation.count));
  }
  g_tmag_calibration.magic = kCalibrationRecordMagic;
  g_tmag_calibration.valid = true;
  g_calibration_bundle.tmag = g_tmag_calibration;
  ConfigStore::saveCalibrationBundle(g_calibration_bundle);
  g_saved = true;
}

void sendFrames() {
  const int16_t ref_cdeg = radToCdeg(g_last_ref_angle_rad);
  const int16_t est_cdeg = g_tmag_calibration.valid ? radToCdeg(g_last_est_angle_rad) : 0;
  const int16_t err_cdeg = g_tmag_calibration.valid ? radToCdeg(g_last_err_rad) : 0;
  const int16_t cal_rms_cdeg = g_tmag_calibration.valid ? radToCdeg(g_tmag_calibration.calibration_rms_rad) : 0;
  const int16_t val_rms_cdeg = (g_validation.count > 0)
                                   ? radToCdeg(sqrtf(g_validation.err2_sum /
                                                     static_cast<float>(g_validation.count)))
                                   : 0;
  const int16_t val_mae_cdeg = (g_validation.count > 0)
                                   ? radToCdeg(g_validation.abs_sum /
                                               static_cast<float>(g_validation.count))
                                   : 0;
  const int16_t val_max_cdeg = radToCdeg(g_validation.max_abs);

  {
    uint8_t data[8] = {0};
    data[0] = 0xF1;
    data[1] = static_cast<uint8_t>(g_phase);
    data[2] = g_tmag_calibration.valid ? 1 : 0;
    data[3] = static_cast<uint8_t>(g_builder.sampleCount() & 0xFFu);
    data[4] = static_cast<uint8_t>((g_builder.sampleCount() >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(g_metrics.valid_bin_count & 0xFFu);
    data[6] = static_cast<uint8_t>((g_metrics.valid_bin_count >> 8) & 0xFFu);
    data[7] = (g_phase == Phase::Error) ? g_failure_code : static_cast<uint8_t>(g_validation.count & 0xFFu);
    CanTransport::sendStd(summaryCanId(), data, 8);
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
    CanTransport::sendStd(angleCanId(), data, 8);
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
    data[7] = static_cast<uint8_t>(g_validation.count & 0xFFu);
    CanTransport::sendStd(statsCanId(), data, 8);
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
    data[7] = static_cast<uint8_t>(val_max_cdeg & 0xFFu);
    CanTransport::sendStd(vectorCanId(), data, 8);
  }
}

void failWith(uint8_t failure_code) {
  g_failure_code = failure_code;
  g_phase = Phase::Error;
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

  if (!ConfigStore::loadActuatorConfig(g_actuator_config)) {
    g_actuator_config = buildDefaultActuatorConfig();
    ConfigStore::saveActuatorConfig(g_actuator_config);
  } else if (syncActuatorConfigToFirmwareDefaults(g_actuator_config)) {
    ConfigStore::saveActuatorConfig(g_actuator_config);
  }
  ConfigStore::loadCalibrationBundle(g_calibration_bundle);

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

  if (g_calibration_bundle.foc.valid) {
    g_motor.sensor_direction = static_cast<Direction>(g_calibration_bundle.foc.sensor_direction);
    g_motor.zero_electric_angle = g_calibration_bundle.foc.zero_electrical_angle;
  }

  digitalWrite(PIN_EN_GATE, HIGH);
  delay(10);
  g_motor.init();
  if (!g_motor.initFOC()) {
    failWith(1);
    return;
  }

  g_calibration_bundle.foc.magic = kCalibrationRecordMagic;
  g_calibration_bundle.foc.sensor_direction = static_cast<int8_t>(g_motor.sensor_direction);
  g_calibration_bundle.foc.zero_electrical_angle = g_motor.zero_electric_angle;
  g_calibration_bundle.foc.valid = true;
  ConfigStore::saveCalibrationBundle(g_calibration_bundle);

  g_builder.reset(kTmagCalibrationGearRatio);
  updateMotorUnwrapped();
}

}  // namespace

void setup() {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  ensureAs5600WireInit();
  updateAs5600Reference();
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

  const float boundary_output_rad = phaseBoundaryOutputRad();
  if (g_phase == Phase::Error) {
    g_motor.move(0.0f);
  } else if (g_phase == Phase::BootDelay) {
    g_motor.move(0.0f);
    if ((uint32_t)(millis() - g_boot_ms) >= kStartDelayMs) {
      g_phase = Phase::Calibrating;
      g_as5600_origin_rad = g_as5600_unwrapped_rad;
    }
  } else if (g_phase == Phase::Calibrating || g_phase == Phase::Validating) {
    const float remaining = g_motor_target_rad - g_motor_unwrapped_rad;
    float target_vel = (remaining >= 0.0f) ? kMotorVelocityRadPerSec : -kMotorVelocityRadPerSec;
    if (fabsf(remaining) <= kMotorTargetToleranceRad) {
      if (g_phase == Phase::Validating) {
        saveCalibrationResult();
      }
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

    if (!updateAs5600Reference()) {
      failWith(2);
    } else {
      readTmagXYZ(g_last_x, g_last_y, g_last_z);

      if (g_phase == Phase::Calibrating) {
        TmagCalibrationSample sample = {};
        sample.x = g_last_x;
        sample.y = g_last_y;
        sample.z = g_last_z;
        sample.input_angle_rad = g_last_motor_raw_rad;
        sample.reference_angle_rad = g_last_ref_angle_rad;
        if (!g_builder.addSample(sample)) {
          failWith(3);
        } else if (fabsf(g_last_ref_angle_rad) >= boundary_output_rad) {
          if (!g_builder.build(g_tmag_calibration, &g_metrics)) {
            failWith(4);
          } else {
            g_phase = Phase::Validating;
          }
        }
      } else if (g_phase == Phase::Validating) {
        if (TmagCalibrationBuilder::estimateAngleRad(
                g_tmag_calibration,
                g_last_x,
                g_last_y,
                g_last_z,
                g_last_motor_raw_rad,
                g_last_est_angle_rad,
                &g_last_best_bin)) {
          g_last_err_rad = wrapPmPi(g_last_est_angle_rad - g_last_ref_angle_rad);
          g_validation.err2_sum += g_last_err_rad * g_last_err_rad;
          g_validation.abs_sum += fabsf(g_last_err_rad);
          if (fabsf(g_last_err_rad) > g_validation.max_abs) {
            g_validation.max_abs = fabsf(g_last_err_rad);
          }
          ++g_validation.count;
          if (fabsf(g_last_ref_angle_rad) >= (2.0f * boundary_output_rad) && !g_saved) {
            saveCalibrationResult();
          }
        }
      }
    }
  }

  if ((uint32_t)(now - last_status_ms) >= kStatusPeriodMs) {
    last_status_ms = now;
    sendFrames();
  }
}
