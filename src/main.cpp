#include "app.h"
#include "actuator_api.h"
#include "can_protocol.h"
#include "can_transport.h"
#include "config/actuator_defaults.h"
#include "config/calibration_constants.h"

#include <math.h>

extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  // 16MHz crystal
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON; // <-- crystal/resonator
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

  // VCOin = 16/16 = 1MHz
  RCC_OscInitStruct.PLL.PLLM = 16;
  // VCOout = 1MHz * 360 = 360MHz
  RCC_OscInitStruct.PLL.PLLN = 360;
  // SYSCLK = 360/2 = 180MHz
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;

  // If you don't use USB from the MCU, PLLQ isn't critical for Arduino timing
  RCC_OscInitStruct.PLL.PLLQ = 7;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    while (1) {}
  }

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
      RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4; // 45MHz
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2; // 90MHz

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    while (1) {}
  }
}

AS5048A_CustomSensor sensor(PIN_AS5048_CS, SPI);
BLDCMotor motor(POLE_PAIRS);
BLDCDriver3PWM driver(PIN_PWM_A, PIN_PWM_B, PIN_PWM_C);

MultiTurnEstimator mt;
PositionVelocityController pvc;
ActuatorConfig actuator_config;
OutputEncoderManager output_encoder_manager;
static bool g_power_stage_armed = false;
static float g_applied_output_velocity_deg_s = 0.0f;
static uint32_t g_last_velocity_cmd_ms = 0;
static float g_applied_angle_mode_output_velocity_deg_s = 0.0f;
static uint32_t g_last_angle_mode_cmd_ms = 0;

static void resetVelocityCommandRamp();
static void resetAngleModeCommandRamp();
static void disarmPowerStage();
static bool armPowerStage();

namespace {
constexpr uint16_t kCalPressMs = 1000;
constexpr uint16_t kClearPressMs = 3000;
constexpr uint16_t kManualZeroPressMs = 5000;
constexpr uint16_t kRuntimeDiagCanIdBase = 0x5F0;
constexpr uint32_t kRuntimeDiagPeriodMs = 500;
constexpr float kMinGearRatio = 0.001f;
constexpr float kMaxGearRatio = 1000.0f;
constexpr float kMaxConfigAbsDeg = 1000000.0f;
constexpr float kAs5600AutoCalOutputVelocityDegS = 10.0f;
constexpr uint32_t kAs5600AutoCalMoveMs = 500;
constexpr float kAs5600AutoCalMinDeltaRad = 0.02f;
bool g_need_calibration = false;

enum class ProfileSelectResult : uint8_t {
  None = 0,
  Ok = 1,
  RejectedArmed = 2,
  As5600ReadFailed = 3,
  NotSelectable = 4,
  SaveFailed = 5,
};

ProfileSelectResult g_last_profile_select_result = ProfileSelectResult::None;

void clearCalibrationData() {
  ConfigStore::clearCalibrationBundleCompat();
}

bool outputEncoderRequired(const ActuatorConfig& config) {
  return config.output_encoder_type != OutputEncoderType::VelocityOnly;
}

bool isOutputProfileSelectable(const ConfigStore::CalibrationBundle& bundle,
                               OutputEncoderType profile) {
  switch (profile) {
    case OutputEncoderType::VelocityOnly:
      return true;
    case OutputEncoderType::DirectInput:
      return isDirectInputCompatible(actuator_config);
    case OutputEncoderType::As5600:
      return bundle.as5600.valid;
    case OutputEncoderType::TmagLut:
      return bundle.tmag.valid;
  }
  return false;
}

bool hasRequiredOutputCalibration(const ConfigStore::CalibrationBundle& bundle,
                                  const ActuatorConfig& config) {
  switch (config.output_encoder_type) {
    case OutputEncoderType::VelocityOnly:
      return true;
    case OutputEncoderType::DirectInput:
      return true;
    case OutputEncoderType::As5600:
      return bundle.as5600.valid;
    case OutputEncoderType::TmagLut:
      return bundle.tmag.valid &&
             fabsf(bundle.tmag.learned_gear_ratio - config.gear_ratio) <= 0.001f;
  }
  return false;
}

bool hasRequiredMotionCalibration(const ConfigStore::CalibrationBundle& bundle,
                                  const ActuatorConfig& config) {
  return bundle.foc.valid && hasRequiredOutputCalibration(bundle, config);
}

bool readOutputEncoderAbsolute(float& out_angle_rad);
bool readOutputEncoderZeroRelative(float& out_angle_rad);

bool validTravelLimits(float output_min_deg, float output_max_deg) {
  return isfinite(output_min_deg) && isfinite(output_max_deg) &&
         fabsf(output_min_deg) <= kMaxConfigAbsDeg &&
         fabsf(output_max_deg) <= kMaxConfigAbsDeg &&
         output_max_deg > output_min_deg;
}

bool validGearRatio(float gear_ratio) {
  return isfinite(gear_ratio) &&
         gear_ratio >= kMinGearRatio &&
         gear_ratio <= kMaxGearRatio;
}

bool prepareAs5600Profile(ConfigStore::CalibrationBundle& bundle,
                          ProfileSelectResult& failure_result) {
  float angle_rad = 0.0f;
  if (!readAs5600AngleRad(angle_rad)) {
    failure_result = ProfileSelectResult::As5600ReadFailed;
    return false;
  }

  if (!bundle.as5600.valid) {
    bundle.as5600 = {};
    bundle.as5600.magic = kCalibrationRecordMagic;
    bundle.as5600.zero_offset_rad = angle_rad;
    bundle.as5600.invert = false;
    bundle.as5600.valid = true;
    if (!ConfigStore::saveCalibrationBundleCompat(bundle)) {
      failure_result = ProfileSelectResult::SaveFailed;
      return false;
    }
  }

  return true;
}

void applyOutputZeroReferenceForProfile(const ConfigStore::CalibrationBundle& bundle,
                                        OutputEncoderType profile) {
  switch (profile) {
    case OutputEncoderType::VelocityOnly:
      ActuatorAPI::output_zero_ref_rad = 0.0f;
      break;
    case OutputEncoderType::DirectInput:
      ActuatorAPI::output_zero_ref_rad =
          bundle.direct_input.valid ? bundle.direct_input.zero_offset_rad : 0.0f;
      break;
    case OutputEncoderType::As5600:
      ActuatorAPI::output_zero_ref_rad =
          bundle.as5600.valid ? bundle.as5600.zero_offset_rad : 0.0f;
      break;
    case OutputEncoderType::TmagLut:
      ActuatorAPI::output_zero_ref_rad =
          bundle.tmag.valid ? bundle.tmag.zero_offset_rad : 0.0f;
      break;
  }
}

uint16_t runtimeDiagCanId() {
  return kRuntimeDiagCanIdBase + actuator_config.can_node_id;
}

OutputEncoderType activeOutputEncoderType() {
  const IOutputEncoder* active_encoder = output_encoder_manager.active();
  if (active_encoder == nullptr) {
    return OutputEncoderType::VelocityOnly;
  }
  return active_encoder->type();
}

void sendRuntimeDiagIfDue() {
  static uint32_t last_diag_ms = 0;
  const uint32_t now_ms = millis();
  if ((uint32_t)(now_ms - last_diag_ms) < kRuntimeDiagPeriodMs) {
    return;
  }
  last_diag_ms = now_ms;

  uint8_t data[8] = {0};
  data[0] = 0xFB;
  data[1] = static_cast<uint8_t>(actuator_config.output_encoder_type);
  data[2] = static_cast<uint8_t>(activeOutputEncoderType());
  data[3] = static_cast<uint8_t>(actuator_config.default_control_mode);
  data[4] = actuator_config.enable_velocity_mode ? 1 : 0;
  data[5] = actuator_config.enable_output_angle_mode ? 1 : 0;
  data[6] = (g_need_calibration ? 0x01 : 0x00) |
            (static_cast<uint8_t>(g_last_profile_select_result) << 4);
  data[7] = 0;
  if (outputEncoderRequired(actuator_config)) {
    data[7] |= 0x01;
  }
  if (g_power_stage_armed) {
    data[7] |= 0x02;
  }
  CanTransport::sendStd(runtimeDiagCanId(), data, 8);

  uint8_t limits_data[8] = {0};
  uint8_t limits_len = 0;
  if (CanProtocol::encodeActuatorLimitsStatus_OptionA(
          actuator_config.output_min_deg,
          actuator_config.output_max_deg,
          limits_data,
          limits_len)) {
    CanTransport::sendStd(
        CanProtocol::actuatorLimitsStatusCanId(actuator_config.can_node_id),
        limits_data,
        limits_len);
  }

  uint8_t config_data[8] = {0};
  uint8_t config_len = 0;
  if (CanProtocol::encodeActuatorConfigStatus_OptionA(
          actuator_config.gear_ratio,
          actuator_config.output_encoder_type,
          actuator_config.default_control_mode,
          actuator_config.enable_velocity_mode,
          actuator_config.enable_output_angle_mode,
          config_data,
          config_len)) {
    CanTransport::sendStd(
        CanProtocol::actuatorConfigStatusCanId(actuator_config.can_node_id),
        config_data,
        config_len);
  }
}

void refreshBootReference() {
  sensor.update();
  const float raw = sensor.getAngle();
  mt.reset(raw);

  float output_boot_delta_rad = 0.0f;
  const bool have_output_boot_reference =
      outputEncoderRequired(actuator_config) && readOutputEncoderZeroRelative(output_boot_delta_rad);
  if (have_output_boot_reference) {
    ActuatorAPI::setBootReferenceFromOutputDelta(mt.mt_angle, output_boot_delta_rad);
  } else {
    ActuatorAPI::setBootReferenceFromMotor(mt.mt_angle);
  }
  // With an absolute output reference, boot into an output-zero target. The power
  // stage still remains disarmed until an explicit CAN arm command is received.
  ActuatorAPI::target_output_deg =
      have_output_boot_reference ? 0.0f : ActuatorAPI::motorMTToOutputRawDeg(mt.mt_angle);
  ActuatorAPI::target_output_velocity_deg_s = 0.0f;
}

void reconfigureRuntimeAfterActuatorConfigChange(float current_motor_mt_rad,
                                                 bool refresh_boot_reference) {
  ConfigStore::CalibrationBundle calibration_bundle = {};
  ConfigStore::loadCalibrationBundleCompat(calibration_bundle);

  CanService::configure(actuator_config);
  ActuatorAPI::configure(actuator_config);
  output_encoder_manager.configure(
      actuator_config,
      calibration_bundle.direct_input.valid ? &calibration_bundle.direct_input : nullptr,
      calibration_bundle.as5600.valid ? &calibration_bundle.as5600 : nullptr,
      calibration_bundle.tmag.valid ? &calibration_bundle.tmag : nullptr,
      &sensor);
  g_need_calibration = !hasRequiredMotionCalibration(calibration_bundle, actuator_config);

  if (refresh_boot_reference) {
    refreshBootReference();
  } else {
    ActuatorAPI::target_output_deg =
        ActuatorAPI::motorMTToOutputRawDeg(current_motor_mt_rad);
    ActuatorAPI::target_output_velocity_deg_s = 0.0f;
  }
  resetVelocityCommandRamp();
  resetAngleModeCommandRamp();
}

bool applyActuatorLimitsConfig(float output_min_deg,
                               float output_max_deg,
                               float current_motor_mt_rad) {
  if (g_power_stage_armed || !validTravelLimits(output_min_deg, output_max_deg)) {
    return false;
  }

  actuator_config.output_min_deg = output_min_deg;
  actuator_config.output_max_deg = output_max_deg;
  if (!ConfigStore::saveActuatorConfig(actuator_config)) {
    return false;
  }

  reconfigureRuntimeAfterActuatorConfigChange(current_motor_mt_rad, false);
  return true;
}

bool applyActuatorGearConfig(float gear_ratio, float current_motor_mt_rad) {
  if (g_power_stage_armed || !validGearRatio(gear_ratio)) {
    return false;
  }

  ActuatorConfig next_config = actuator_config;
  next_config.gear_ratio = gear_ratio;
  if (next_config.output_encoder_type == OutputEncoderType::DirectInput &&
      !isDirectInputCompatible(next_config)) {
    return false;
  }

  actuator_config = next_config;
  if (!ConfigStore::saveActuatorConfig(actuator_config)) {
    return false;
  }

  reconfigureRuntimeAfterActuatorConfigChange(current_motor_mt_rad, true);
  return true;
}

bool applyOutputEncoderConfig(OutputEncoderType encoder_type,
                              bool invert,
                              float current_motor_mt_rad) {
  if (g_power_stage_armed || encoder_type != OutputEncoderType::As5600) {
    return false;
  }

  ConfigStore::CalibrationBundle calibration_bundle = {};
  ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
  if (!calibration_bundle.as5600.valid) {
    return false;
  }

  calibration_bundle.as5600.magic = kCalibrationRecordMagic;
  calibration_bundle.as5600.invert = invert;
  calibration_bundle.as5600.valid = true;
  if (!ConfigStore::saveCalibrationBundleCompat(calibration_bundle)) {
    return false;
  }

  reconfigureRuntimeAfterActuatorConfigChange(current_motor_mt_rad, true);
  return true;
}

float wrapToPi(float angle_rad) {
  while (angle_rad > PI) angle_rad -= 2.0f * PI;
  while (angle_rad < -PI) angle_rad += 2.0f * PI;
  return angle_rad;
}

bool applyOutputEncoderAutoCalibration(OutputEncoderType encoder_type,
                                       float current_motor_mt_rad) {
  if (g_power_stage_armed || encoder_type != OutputEncoderType::As5600 ||
      actuator_config.output_encoder_type != OutputEncoderType::As5600) {
    return false;
  }

  ConfigStore::CalibrationBundle calibration_bundle = {};
  ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
  if (!calibration_bundle.as5600.valid) {
    return false;
  }

  float raw_before = 0.0f;
  if (!readAs5600AngleRad(raw_before)) {
    return false;
  }

  sensor.update();
  mt.reset(sensor.getAngle());
  if (!armPowerStage()) {
    return false;
  }

  const uint32_t start_ms = millis();
  const float motor_velocity_cmd =
      ActuatorAPI::outputVelocityDegPerSecToMotorVelocity(kAs5600AutoCalOutputVelocityDegS);
  while ((uint32_t)(millis() - start_ms) < kAs5600AutoCalMoveMs) {
    motor.loopFOC();
    sensor.update();
    mt.update(sensor.getAngle());
    motor.move(motor_velocity_cmd);
  }

  motor.move(0.0f);
  for (uint8_t i = 0; i < 20; ++i) {
    motor.loopFOC();
    motor.move(0.0f);
    delay(2);
  }

  float raw_after = 0.0f;
  const bool read_after_ok = readAs5600AngleRad(raw_after);

  sensor.update();
  const float pos_after = mt.update(sensor.getAngle());
  disarmPowerStage();

  if (!read_after_ok) {
    reconfigureRuntimeAfterActuatorConfigChange(pos_after, true);
    return false;
  }

  const float raw_delta = wrapToPi(raw_after - raw_before);
  if (fabsf(raw_delta) < kAs5600AutoCalMinDeltaRad) {
    reconfigureRuntimeAfterActuatorConfigChange(pos_after, true);
    return false;
  }

  calibration_bundle.as5600.magic = kCalibrationRecordMagic;
  calibration_bundle.as5600.invert = (raw_delta < 0.0f);
  calibration_bundle.as5600.valid = true;
  if (!ConfigStore::saveCalibrationBundleCompat(calibration_bundle)) {
    reconfigureRuntimeAfterActuatorConfigChange(pos_after, true);
    return false;
  }

  reconfigureRuntimeAfterActuatorConfigChange(pos_after, true);
  return true;
}

bool selectOutputProfile(OutputEncoderType profile) {
  g_last_profile_select_result = ProfileSelectResult::None;
  ConfigStore::CalibrationBundle calibration_bundle = {};
  ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
  if (profile == OutputEncoderType::As5600 &&
      !prepareAs5600Profile(calibration_bundle, g_last_profile_select_result)) {
    return false;
  }
  if (!isOutputProfileSelectable(calibration_bundle, profile)) {
    g_last_profile_select_result = ProfileSelectResult::NotSelectable;
    return false;
  }

  applyOutputProfileDefaults(actuator_config, profile);
  if (!ConfigStore::saveActuatorConfig(actuator_config)) {
    g_last_profile_select_result = ProfileSelectResult::SaveFailed;
    return false;
  }

  CanService::configure(actuator_config);
  ActuatorAPI::configure(actuator_config);
  applyOutputZeroReferenceForProfile(calibration_bundle, profile);
  output_encoder_manager.configure(
      actuator_config,
      calibration_bundle.direct_input.valid ? &calibration_bundle.direct_input : nullptr,
      calibration_bundle.as5600.valid ? &calibration_bundle.as5600 : nullptr,
      calibration_bundle.tmag.valid ? &calibration_bundle.tmag : nullptr,
      &sensor);
  g_need_calibration = !hasRequiredMotionCalibration(calibration_bundle, actuator_config);
  refreshBootReference();
  g_last_profile_select_result = ProfileSelectResult::Ok;
  return true;
}

bool cycleOutputProfile() {
  constexpr OutputEncoderType kProfiles[] = {
      OutputEncoderType::VelocityOnly,
      OutputEncoderType::DirectInput,
      OutputEncoderType::As5600,
      OutputEncoderType::TmagLut,
  };

  int current_idx = 0;
  for (size_t i = 0; i < (sizeof(kProfiles) / sizeof(kProfiles[0])); ++i) {
    if (kProfiles[i] == actuator_config.output_encoder_type) {
      current_idx = static_cast<int>(i);
      break;
    }
  }

  for (size_t step = 1; step <= (sizeof(kProfiles) / sizeof(kProfiles[0])); ++step) {
    const int idx = (current_idx + static_cast<int>(step)) %
                    static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));
    if (selectOutputProfile(kProfiles[idx])) {
      return true;
    }
  }

  return false;
}

void configureOutputEncoder(const ConfigStore::CalibrationBundle& bundle) {
  if (!ConfigStore::loadActuatorConfig(actuator_config)) {
    actuator_config = buildLegacyActuatorConfig();
    ConfigStore::saveActuatorConfig(actuator_config);
  } else if (migrateStaleActuatorConfigDefaults(actuator_config)) {
    ConfigStore::saveActuatorConfig(actuator_config);
  }
  ActuatorAPI::configure(actuator_config);
  output_encoder_manager.configure(
      actuator_config,
      bundle.direct_input.valid ? &bundle.direct_input : nullptr,
      bundle.as5600.valid ? &bundle.as5600 : nullptr,
      bundle.tmag.valid ? &bundle.tmag : nullptr,
      &sensor);
}

bool readOutputEncoderAbsolute(float& out_angle_rad) {
  return output_encoder_manager.readAbsoluteAngleRad(out_angle_rad);
}

bool readOutputEncoderZeroRelative(float& out_angle_rad) {
  return output_encoder_manager.readZeroRelativeAngleRad(out_angle_rad);
}

void updateOutputEncoderZeroOffset(float zero_rad) {
  output_encoder_manager.updateZeroOffset(zero_rad, kCalibrationRecordMagic);
}

enum class ButtonAction {
  None,
  Calibrate,
  ClearStorage,
  ManualZeroMode,
};

ButtonAction updateLongPress(bool is_pressed) {
  static bool was_pressed = false;
  static uint32_t press_start_ms = 0;

  if (is_pressed) {
    if (!was_pressed) {
      press_start_ms = millis();
    }
    was_pressed = true;
    return ButtonAction::None;
  }

  if (was_pressed) {
    uint32_t held_ms = millis() - press_start_ms;
    was_pressed = false;
    if (held_ms >= kManualZeroPressMs) return ButtonAction::ManualZeroMode;
    if (held_ms >= kClearPressMs) return ButtonAction::ClearStorage;
    if (held_ms >= kCalPressMs) return ButtonAction::Calibrate;
  }
  return ButtonAction::None;
}
} // namespace

static void motorBeep(int freq_hz = 800, int duration_ms = 120, float uq_volts = 1.0f) {
  if (freq_hz < 50) freq_hz = 50;
  if (freq_hz > 2000) freq_hz = 2000;
  if (duration_ms < 20) duration_ms = 20;

  motor.enable();
  const uint32_t half_period_us = 1000000UL / (2UL * (uint32_t)freq_hz);
  const uint32_t total_us = (uint32_t)duration_ms * 1000UL;
  uint32_t t0 = micros();
  bool flip = false;

  while ((micros() - t0) < total_us) {
    flip = !flip;
    float elec_angle = flip ? 0.0f : PI;
    motor.setPhaseVoltage(uq_volts, 0.0f, elec_angle);
    delayMicroseconds(half_period_us);
  }

  motor.setPhaseVoltage(0.0f, 0.0f, 0.0f);
}

static void motorBeepDouble() {
  motorBeep();
  delay(120);
  motorBeep();
}

static void resetVelocityCommandRamp() {
  g_applied_output_velocity_deg_s = 0.0f;
  g_last_velocity_cmd_ms = millis();
}

static void resetAngleModeCommandRamp() {
  g_applied_angle_mode_output_velocity_deg_s = 0.0f;
  g_last_angle_mode_cmd_ms = millis();
}

static float slewOutputVelocityCommand(float target_output_velocity_deg_s) {
  const uint32_t now_ms = millis();
  float dt_s = 0.0f;
  if (g_last_velocity_cmd_ms != 0 && now_ms >= g_last_velocity_cmd_ms) {
    dt_s = static_cast<float>(now_ms - g_last_velocity_cmd_ms) * 0.001f;
  }
  g_last_velocity_cmd_ms = now_ms;

  if (dt_s <= 0.0f) {
    return g_applied_output_velocity_deg_s;
  }

  const float max_delta = ACTUATOR_OUTPUT_VELOCITY_SLEW_DEG_S2 * dt_s;
  const float delta = target_output_velocity_deg_s - g_applied_output_velocity_deg_s;
  if (delta > max_delta) {
    g_applied_output_velocity_deg_s += max_delta;
  } else if (delta < -max_delta) {
    g_applied_output_velocity_deg_s -= max_delta;
  } else {
    g_applied_output_velocity_deg_s = target_output_velocity_deg_s;
  }
  return g_applied_output_velocity_deg_s;
}

static float slewAngleModeVelocityCommand(float target_motor_velocity_cmd) {
  const float target_output_velocity_deg_s =
      ActuatorAPI::motorVelocityToOutputVelocityDegPerSec(target_motor_velocity_cmd);

  const uint32_t now_ms = millis();
  float dt_s = 0.0f;
  if (g_last_angle_mode_cmd_ms != 0 && now_ms >= g_last_angle_mode_cmd_ms) {
    dt_s = static_cast<float>(now_ms - g_last_angle_mode_cmd_ms) * 0.001f;
  }
  g_last_angle_mode_cmd_ms = now_ms;

  if (dt_s <= 0.0f) {
    return ActuatorAPI::outputVelocityDegPerSecToMotorVelocity(
        g_applied_angle_mode_output_velocity_deg_s);
  }

  const float max_delta = ACTUATOR_OUTPUT_ANGLE_MODE_SLEW_DEG_S2 * dt_s;
  const float delta =
      target_output_velocity_deg_s - g_applied_angle_mode_output_velocity_deg_s;
  if (delta > max_delta) {
    g_applied_angle_mode_output_velocity_deg_s += max_delta;
  } else if (delta < -max_delta) {
    g_applied_angle_mode_output_velocity_deg_s -= max_delta;
  } else {
    g_applied_angle_mode_output_velocity_deg_s = target_output_velocity_deg_s;
  }

  return ActuatorAPI::outputVelocityDegPerSecToMotorVelocity(
      g_applied_angle_mode_output_velocity_deg_s);
}

static float applyAngleModeEdgeBraking(float motor_velocity_cmd,
                                       float current_output_raw_rad,
                                       float output_min_rad,
                                       float output_max_rad) {
  const float output_velocity_deg_s =
      ActuatorAPI::motorVelocityToOutputVelocityDegPerSec(motor_velocity_cmd);
  if (fabsf(output_velocity_deg_s) <= 0.0f) {
    return motor_velocity_cmd;
  }

  float remaining_rad = 0.0f;
  if (output_velocity_deg_s > 0.0f) {
    remaining_rad = output_max_rad - current_output_raw_rad;
  } else {
    remaining_rad = current_output_raw_rad - output_min_rad;
  }

  if (remaining_rad <= 0.0f) {
    return 0.0f;
  }

  const float remaining_deg = remaining_rad * (180.0f / PI);
  const float output_accel_deg_s2 = ACTUATOR_OUTPUT_EDGE_BRAKE_DEG_S2;
  if (output_accel_deg_s2 <= 0.0f) {
    return motor_velocity_cmd;
  }

  // Kinematic braking cap: v^2 <= 2 a d
  const float max_output_speed_deg_s = sqrtf(2.0f * output_accel_deg_s2 * remaining_deg);
  if (fabsf(output_velocity_deg_s) <= max_output_speed_deg_s) {
    return motor_velocity_cmd;
  }

  const float capped_output_velocity_deg_s =
      (output_velocity_deg_s > 0.0f) ? max_output_speed_deg_s : -max_output_speed_deg_s;
  return ActuatorAPI::outputVelocityDegPerSecToMotorVelocity(capped_output_velocity_deg_s);
}

static void disarmPowerStage() {
  motor.disable();
  digitalWrite(PIN_EN_GATE, LOW);
  ActuatorAPI::target_output_velocity_deg_s = 0.0f;
  resetVelocityCommandRamp();
  resetAngleModeCommandRamp();
  g_power_stage_armed = false;
}

static bool armPowerStage() {
  if (g_power_stage_armed) {
    return true;
  }

  ConfigStore::CalibrationBundle calibration_bundle = {};
  ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
  if (calibration_bundle.foc.valid) {
    motor.sensor_direction = (Direction)calibration_bundle.foc.sensor_direction;
    motor.zero_electric_angle = calibration_bundle.foc.zero_electrical_angle;
  }

  digitalWrite(PIN_EN_GATE, HIGH);
  delay(10);

  motor.init();
  const int foc_ok = motor.initFOC();
  if (!foc_ok) {
    disarmPowerStage();
    g_need_calibration = true;
    return false;
  }

  if (!calibration_bundle.foc.valid) {
    calibration_bundle.foc = {};
    calibration_bundle.foc.magic = kCalibrationRecordMagic;
    calibration_bundle.foc.sensor_direction = (int8_t)motor.sensor_direction;
    calibration_bundle.foc.zero_electrical_angle = motor.zero_electric_angle;
    calibration_bundle.foc.valid = true;
    ConfigStore::saveCalibrationBundleCompat(calibration_bundle);
  }

  if (actuator_config.output_encoder_type == OutputEncoderType::As5600 &&
      !calibration_bundle.as5600.valid) {
    float as5600_zero_rad = 0.0f;
    if (!readAs5600AngleRad(as5600_zero_rad)) {
      disarmPowerStage();
      g_need_calibration = true;
      return false;
    }
    calibration_bundle.as5600 = {};
    calibration_bundle.as5600.magic = kCalibrationRecordMagic;
    calibration_bundle.as5600.zero_offset_rad = as5600_zero_rad;
    calibration_bundle.as5600.invert = false;
    calibration_bundle.as5600.valid = true;
    ActuatorAPI::output_zero_ref_rad = as5600_zero_rad;
    ConfigStore::saveCalibrationBundleCompat(calibration_bundle);
  }

  g_need_calibration = !hasRequiredMotionCalibration(calibration_bundle, actuator_config);
  resetVelocityCommandRamp();
  resetAngleModeCommandRamp();
  g_power_stage_armed = true;
  return true;
}

static void initSystem() {
  // ---------- DRV8302 strap pins ----------
  pinMode(PIN_EN_GATE, OUTPUT);
  pinMode(PIN_M_PWM, OUTPUT);
  pinMode(PIN_M_OC, OUTPUT);
  pinMode(PIN_OC_GAIN, OUTPUT);

  // 3PWM mode straps (board design)
  digitalWrite(PIN_M_PWM, HIGH);
  digitalWrite(PIN_M_OC, HIGH);
  digitalWrite(PIN_OC_GAIN, HIGH);

  // Gate disable initially
  digitalWrite(PIN_EN_GATE, LOW);

  // ---------- SPI & Sensor ----------
  pinMode(PIN_AS5048_CS, OUTPUT);
  digitalWrite(PIN_AS5048_CS, HIGH);
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);
  pinMode(PIN_SPI1_NCS_3, OUTPUT);
  digitalWrite(PIN_SPI1_NCS_3, HIGH);
  SPI.begin();
  FM25CL64B::begin();
  sensor.init();
  sensor.update();

  // ---------- AS5600 bootstrap (setup only) ----------
  // Read later (after FOC init) so AS5600 and motor MT are aligned in time.

  // ---------- CAN ----------
  if (!ConfigStore::loadActuatorConfig(actuator_config)) {
    actuator_config = buildLegacyActuatorConfig();
    ConfigStore::saveActuatorConfig(actuator_config);
  } else if (migrateStaleActuatorConfigDefaults(actuator_config)) {
    ConfigStore::saveActuatorConfig(actuator_config);
  }
  ActuatorAPI::configure(actuator_config);
  CanService::init(actuator_config);

  // ---------- Driver ----------
  driver.voltage_power_supply = BUS_VOLTAGE;   // 40.0V
  driver.voltage_limit        = VOLTAGE_LIMIT; // 40.0V
  driver.init();

  // ---------- Motor ----------
  motor.linkDriver(&driver);
  motor.linkSensor(&sensor);

  // Inner loop = velocity, outer loop makes velocity cmd
  motor.controller        = MotionControlType::velocity;
  motor.torque_controller = TorqueControlType::voltage;

  motor.voltage_sensor_align = ALIGN_VOLTAGE;
  motor.voltage_limit        = (TORQUE_LIMIT_VOLTS < VOLTAGE_LIMIT) ? TORQUE_LIMIT_VOLTS : VOLTAGE_LIMIT;

  motor.PID_velocity.P = 0.12;
  motor.PID_velocity.I = 2.0;
  motor.PID_velocity.D = 0.0;
  motor.LPF_velocity.Tf = 0.007;
  motor.velocity_limit = ACTUATOR_MOTOR_VELOCITY_LIMIT_RAD_S;

  // ---------- Calibration gate ----------
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  ConfigStore::CalibrationBundle calibration_bundle = {};
  ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
  const bool have_cal = hasRequiredMotionCalibration(calibration_bundle, actuator_config);
  if (calibration_bundle.foc.valid) {
    motor.sensor_direction = (Direction)calibration_bundle.foc.sensor_direction;
    motor.zero_electric_angle = calibration_bundle.foc.zero_electrical_angle;
  }
  switch (actuator_config.output_encoder_type) {
    case OutputEncoderType::VelocityOnly:
      ActuatorAPI::output_zero_ref_rad = 0.0f;
      break;
    case OutputEncoderType::DirectInput:
      ActuatorAPI::output_zero_ref_rad =
          calibration_bundle.direct_input.valid ? calibration_bundle.direct_input.zero_offset_rad
                                                : 0.0f;
      break;
    case OutputEncoderType::As5600:
      ActuatorAPI::output_zero_ref_rad =
          calibration_bundle.as5600.valid ? calibration_bundle.as5600.zero_offset_rad : 0.0f;
      break;
    case OutputEncoderType::TmagLut:
      ActuatorAPI::output_zero_ref_rad =
          calibration_bundle.tmag.valid ? calibration_bundle.tmag.zero_offset_rad : 0.0f;
      break;
  }
  configureOutputEncoder(calibration_bundle);

  g_need_calibration = !hasRequiredMotionCalibration(calibration_bundle, actuator_config);
  refreshBootReference();
  resetVelocityCommandRamp();
  resetAngleModeCommandRamp();
  disarmPowerStage();

  // Outer loop gains (tune)
  pvc.Kp = 2.45f;
  pvc.vel_limit = ACTUATOR_MOTOR_VELOCITY_LIMIT_RAD_S;
  pvc.accel_limit = ACTUATOR_MOTOR_ACCEL_LIMIT_RAD_S2;
  pvc.reset();
}

void setup() {
  initSystem();
}

void loop() {
  static bool calibrating = false;
  static bool manual_zero_mode = false;
  static uint32_t suppress_actions_until_ms = 0;
  ButtonAction action = updateLongPress(digitalRead(PIN_USER_BTN) == LOW);
  if (millis() < suppress_actions_until_ms) {
    action = ButtonAction::None;
  }

  sendRuntimeDiagIfDue();

  if (!g_power_stage_armed) {
    sensor.update();
    const float raw = sensor.getAngle();
    const float pos_mt = mt.update(raw);
    CanService::poll(pos_mt);

    OutputEncoderType requested_profile = OutputEncoderType::As5600;
    if (CanService::takePendingOutputProfileChange(requested_profile)) {
      selectOutputProfile(requested_profile);
      return;
    }

    float requested_output_min_deg = 0.0f;
    float requested_output_max_deg = 0.0f;
    if (CanService::takePendingActuatorLimitsConfig(
            requested_output_min_deg, requested_output_max_deg)) {
      applyActuatorLimitsConfig(requested_output_min_deg, requested_output_max_deg, pos_mt);
      return;
    }

    float requested_gear_ratio = 1.0f;
    if (CanService::takePendingActuatorGearConfig(requested_gear_ratio)) {
      applyActuatorGearConfig(requested_gear_ratio, pos_mt);
      return;
    }

    OutputEncoderType requested_encoder_type = OutputEncoderType::As5600;
    bool requested_encoder_invert = false;
    if (CanService::takePendingOutputEncoderConfig(
            requested_encoder_type, requested_encoder_invert)) {
      applyOutputEncoderConfig(requested_encoder_type, requested_encoder_invert, pos_mt);
      return;
    }

    OutputEncoderType requested_encoder_auto_cal = OutputEncoderType::As5600;
    if (CanService::takePendingOutputEncoderAutoCalibration(requested_encoder_auto_cal)) {
      applyOutputEncoderAutoCalibration(requested_encoder_auto_cal, pos_mt);
      return;
    }

    bool power_stage_enable = false;
    if (CanService::takePendingPowerStageEnable(power_stage_enable) && power_stage_enable) {
      armPowerStage();
    }
    return;
  }

  if (manual_zero_mode) {
    if (action == ButtonAction::ManualZeroMode) {
      motor.enable();
      manual_zero_mode = false;
      motorBeepDouble();
      return;
    }

    if (action == ButtonAction::ClearStorage) {
      if (cycleOutputProfile()) {
        motorBeep();
      }
      return;
    }

    if (action == ButtonAction::Calibrate) {
      if (!outputEncoderRequired(actuator_config)) {
        motor.enable();
        manual_zero_mode = false;
        return;
      }
      float zero_rad = 0.0f;
      if (readOutputEncoderAbsolute(zero_rad)) {
        ActuatorAPI::output_zero_ref_rad = zero_rad;
        updateOutputEncoderZeroOffset(zero_rad);
        ConfigStore::CalibrationBundle calibration_bundle = {};
        ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
        calibration_bundle.foc.magic = kCalibrationRecordMagic;
        calibration_bundle.foc.sensor_direction = (int8_t)motor.sensor_direction;
        calibration_bundle.foc.zero_electrical_angle = motor.zero_electric_angle;
        calibration_bundle.foc.valid = true;
        if (output_encoder_manager.active() != nullptr &&
            output_encoder_manager.active()->type() == OutputEncoderType::TmagLut) {
          calibration_bundle.tmag = output_encoder_manager.tmagCalibration();
          calibration_bundle.tmag.magic = kCalibrationRecordMagic;
        } else if (output_encoder_manager.active() != nullptr &&
                   output_encoder_manager.active()->type() == OutputEncoderType::DirectInput) {
          calibration_bundle.direct_input = output_encoder_manager.directInputCalibration();
          calibration_bundle.direct_input.magic = kCalibrationRecordMagic;
        } else {
          calibration_bundle.as5600 = output_encoder_manager.as5600Calibration();
          calibration_bundle.as5600.magic = kCalibrationRecordMagic;
        }
        ConfigStore::saveCalibrationBundleCompat(calibration_bundle);

        refreshBootReference();
        motor.enable();
        motorBeepDouble();
        manual_zero_mode = false;
      }
    }
    return;
  }

  if (action == ButtonAction::ManualZeroMode && !calibrating) {
    manual_zero_mode = true;
    motor.disable();
    motorBeep();
  }
  if (action == ButtonAction::ClearStorage && !calibrating) {
    clearCalibrationData();
    motorBeepDouble();
    ConfigStore::CalibrationBundle calibration_bundle = {};
    g_need_calibration = !hasRequiredMotionCalibration(calibration_bundle, actuator_config);
    motor.disable();
    suppress_actions_until_ms = millis() + 1500;
  }
  if (action == ButtonAction::Calibrate && !calibrating) {
    calibrating = true;
    motorBeep();
    motor.disable();
    delay(50);

    motor.sensor_direction = Direction::UNKNOWN;
    motor.zero_electric_angle = NOT_SET;
    motor.init();
    int foc_ok = motor.initFOC();
  if (foc_ok) {
      ConfigStore::CalibrationBundle calibration_bundle = {};
      ConfigStore::loadCalibrationBundleCompat(calibration_bundle);
      calibration_bundle.foc.magic = kCalibrationRecordMagic;
      calibration_bundle.foc.sensor_direction = (int8_t)motor.sensor_direction;
      calibration_bundle.foc.zero_electrical_angle = motor.zero_electric_angle;
      calibration_bundle.foc.valid = true;
      if (actuator_config.output_encoder_type == OutputEncoderType::DirectInput) {
        calibration_bundle.direct_input.magic = kCalibrationRecordMagic;
        calibration_bundle.direct_input.zero_offset_rad = ActuatorAPI::output_zero_ref_rad;
        calibration_bundle.direct_input.valid = true;
      } else if (actuator_config.output_encoder_type == OutputEncoderType::As5600) {
        calibration_bundle.as5600.magic = kCalibrationRecordMagic;
        calibration_bundle.as5600.zero_offset_rad = ActuatorAPI::output_zero_ref_rad;
        calibration_bundle.as5600.invert = false;
        calibration_bundle.as5600.valid = true;
      }
      ConfigStore::saveCalibrationBundleCompat(calibration_bundle);
      motorBeepDouble();
      g_need_calibration = !hasRequiredMotionCalibration(calibration_bundle, actuator_config);
    }

    refreshBootReference();

    calibrating = false;
  }

  if (calibrating || g_need_calibration) {
    return;
  }

  // FOC loop
  motor.loopFOC();

  // Update sensor & multi-turn
  sensor.update();
  float raw = sensor.getAngle();
  float pos_mt = mt.update(raw);

  // CAN polling and runtime status are based on motor-side multi-turn estimation.
  // Output encoders are used only during boot/alignment and explicit zero capture.
  CanService::poll(pos_mt);

  bool power_stage_enable = false;
  if (CanService::takePendingPowerStageEnable(power_stage_enable)) {
    if (!power_stage_enable) {
      disarmPowerStage();
      return;
    }
  }

  OutputEncoderType requested_profile = OutputEncoderType::As5600;
  if (CanService::takePendingOutputProfileChange(requested_profile)) {
    // Profile/config changes are intentionally ignored while armed.
    g_last_profile_select_result = ProfileSelectResult::RejectedArmed;
  }

  float requested_output_min_deg = 0.0f;
  float requested_output_max_deg = 0.0f;
  if (CanService::takePendingActuatorLimitsConfig(
          requested_output_min_deg, requested_output_max_deg)) {
    // Travel/config changes are intentionally ignored while armed.
  }

  float requested_gear_ratio = 1.0f;
  if (CanService::takePendingActuatorGearConfig(requested_gear_ratio)) {
    // Travel/config changes are intentionally ignored while armed.
  }

  OutputEncoderType requested_encoder_type = OutputEncoderType::As5600;
  bool requested_encoder_invert = false;
  if (CanService::takePendingOutputEncoderConfig(
          requested_encoder_type, requested_encoder_invert)) {
    // Travel/config changes are intentionally ignored while armed.
  }

  OutputEncoderType requested_encoder_auto_cal = OutputEncoderType::As5600;
  if (CanService::takePendingOutputEncoderAutoCalibration(requested_encoder_auto_cal)) {
    // Travel/config changes are intentionally ignored while armed.
  }

  if (ActuatorAPI::active_control_mode == ControlMode::OutputVelocity) {
    float target_output_velocity_deg_s = ActuatorAPI::target_output_velocity_deg_s;
    const float slewed_output_velocity_deg_s =
        slewOutputVelocityCommand(target_output_velocity_deg_s);
    resetAngleModeCommandRamp();
    const float motor_velocity_cmd =
        ActuatorAPI::outputVelocityDegPerSecToMotorVelocity(
            slewed_output_velocity_deg_s);
    motor.move(motor_velocity_cmd);
    return;
  }

  resetVelocityCommandRamp();

  // Output target (boot-centered) -> motor multi-turn target
  float target_output_abs = ActuatorAPI::getTargetOutputAbsRad();
  float current_output_raw = ActuatorAPI::motorMTToOutputRawRad(pos_mt);

  // Hard limit: clamp normal in-range commands, but if the actuator is already
  // outside the configured range, allow hold-current and inward recovery commands.
  const float output_max_rad = actuator_config.output_max_deg * (PI / 180.0f);
  const float output_min_rad = actuator_config.output_min_deg * (PI / 180.0f);
  if (current_output_raw < output_min_rad) {
    if (target_output_abs < current_output_raw) {
      target_output_abs = current_output_raw;
    } else if (target_output_abs > output_max_rad) {
      target_output_abs = output_max_rad;
    }
  } else if (current_output_raw > output_max_rad) {
    if (target_output_abs > current_output_raw) {
      target_output_abs = current_output_raw;
    } else if (target_output_abs < output_min_rad) {
      target_output_abs = output_min_rad;
    }
  } else {
    if (target_output_abs > output_max_rad) {
      target_output_abs = output_max_rad;
    } else if (target_output_abs < output_min_rad) {
      target_output_abs = output_min_rad;
    }
  }

  float motor_target_mt =
      ActuatorAPI::outputDegToMotorMT(target_output_abs * (180.0f / PI), pos_mt);

  // Outer position -> inner velocity command
  float vel_cmd = pvc.compute(motor_target_mt, pos_mt);
  vel_cmd = applyAngleModeEdgeBraking(vel_cmd, current_output_raw, output_min_rad, output_max_rad);
  vel_cmd = slewAngleModeVelocityCommand(vel_cmd);

  // Apply velocity command
  motor.move(vel_cmd);
}
