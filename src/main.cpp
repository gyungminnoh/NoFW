#include "app.h"

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

namespace {
constexpr uint32_t kCalMagic = 0x43414C32; // "CAL2"
constexpr uint16_t kCalAddr = 0;
constexpr uint16_t kCalPressMs = 1000;
constexpr uint16_t kClearPressMs = 3000;
constexpr uint16_t kManualZeroPressMs = 5000;
struct CalData {
  uint32_t magic;
  int8_t sensor_dir;
  float zero_elec;
  float as5600_zero_ref;
};

bool loadCalibration(CalData& out) {
  if (!FM25CL64B::readObject(kCalAddr, out)) return false;
  if (out.magic != kCalMagic) return false;
  if (out.sensor_dir != 1 && out.sensor_dir != -1) return false;
  return true;
}

void saveCalibration(const CalData& in) {
  FM25CL64B::writeObject(kCalAddr, in);
}

void clearCalibration() {
  const uint32_t empty_magic = 0;
  FM25CL64B::writeObject(kCalAddr, empty_magic);
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
  CanService::init();

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

  motor.PID_velocity.P = 0.1;
  motor.PID_velocity.I = 0.2;
  motor.PID_velocity.D = 0.0;
  motor.LPF_velocity.Tf = 0.007;

  // ---------- Calibration gate ----------
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  CalData cal = {};
  const bool have_cal = loadCalibration(cal);
  if (have_cal) {
    motor.sensor_direction = (Direction)cal.sensor_dir;
    motor.zero_electric_angle = cal.zero_elec;
    GripperAPI::as5600_zero_ref_rad = cal.as5600_zero_ref;
  } else {
    GripperAPI::as5600_zero_ref_rad = 0.0f;
  }

  // Enable gate
  digitalWrite(PIN_EN_GATE, HIGH);
  delay(10);

  // Init & FOC
  motor.init();
  int foc_ok = motor.initFOC();
  if (foc_ok && !have_cal) {
    CalData out = {};
    out.magic = kCalMagic;
    out.sensor_dir = (int8_t)motor.sensor_direction;
    out.zero_elec = motor.zero_electric_angle;
    out.as5600_zero_ref = GripperAPI::as5600_zero_ref_rad;
    saveCalibration(out);
    motorBeep();
  }

  // Multi-turn reset at current raw motor angle
  float raw0 = sensor.getAngle();
  mt.reset(raw0);

  // Read AS5600 now (after FOC init) so it matches current motor MT.
  float as5600_boot_rad = 0.0f;
  if (!bootstrapOutputOffset(as5600_boot_rad)) {
    as5600_boot_rad = GripperAPI::as5600_zero_ref_rad;
  }

  // Define boot reference:
  // output 0 rad is AS5600 absolute zero (motor_zero_mt aligned accordingly)
  GripperAPI::setBootReference(mt.mt_angle, as5600_boot_rad);
  // Set target output to "open" at boot
  GripperAPI::target_open_percent = GRIP_BOOT_OPEN_PERCENT;

  // Outer loop gains (tune)
  pvc.Kp = 20.0f;
  pvc.vel_limit = 20.0f;
  pvc.accel_limit = 0.5f;
  pvc.reset();
}

void setup() {
  initSystem();
}

void loop() {
  static bool calibrating = false;
  static bool manual_zero_mode = false;
  static bool need_calibration = false;
  static uint32_t suppress_actions_until_ms = 0;
  ButtonAction action = updateLongPress(digitalRead(PIN_USER_BTN) == LOW);
  if (millis() < suppress_actions_until_ms) {
    action = ButtonAction::None;
  }
  if (action == ButtonAction::ManualZeroMode && !calibrating) {
    manual_zero_mode = true;
    motor.disable();
    motorBeep();
  }
  if (action == ButtonAction::ClearStorage && !calibrating) {
    clearCalibration();
    motorBeepDouble();
    need_calibration = true;
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
      CalData out = {};
      out.magic = kCalMagic;
      out.sensor_dir = (int8_t)motor.sensor_direction;
      out.zero_elec = motor.zero_electric_angle;
      out.as5600_zero_ref = GripperAPI::as5600_zero_ref_rad;
      saveCalibration(out);
      motorBeepDouble();
      need_calibration = false;
    }

    sensor.update();
    float raw = sensor.getAngle();
    mt.reset(raw);
    float as5600_boot_rad = 0.0f;
    if (!bootstrapOutputOffset(as5600_boot_rad)) {
      as5600_boot_rad = GripperAPI::as5600_zero_ref_rad;
    }
    GripperAPI::setBootReference(mt.mt_angle, as5600_boot_rad);
    GripperAPI::target_open_percent = GRIP_BOOT_OPEN_PERCENT;

    calibrating = false;
  }

  if (manual_zero_mode) {
    if (action == ButtonAction::Calibrate) {
      float zero_rad = 0.0f;
      if (readAs5600AngleRad(zero_rad)) {
        GripperAPI::as5600_zero_ref_rad = zero_rad;
        CalData out = {};
        out.magic = kCalMagic;
        out.sensor_dir = (int8_t)motor.sensor_direction;
        out.zero_elec = motor.zero_electric_angle;
        out.as5600_zero_ref = zero_rad;
        saveCalibration(out);

        sensor.update();
        float raw = sensor.getAngle();
        mt.reset(raw);
        GripperAPI::setBootReference(mt.mt_angle, zero_rad);
        GripperAPI::target_open_percent = GRIP_BOOT_OPEN_PERCENT;
        motor.enable();
        motorBeepDouble();
        manual_zero_mode = false;
      }
    }
    return;
  }

  if (calibrating || need_calibration) {
    return;
  }

  // FOC loop
  motor.loopFOC();

  // Update sensor & multi-turn
  sensor.update();
  float raw = sensor.getAngle();
  float pos_mt = mt.update(raw);

  // CAN polling (timeout policy: HOLD-CURRENT)
  CanService::poll(pos_mt);

  // Output target (boot-centered) -> motor multi-turn target
  float target_output_abs = GripperAPI::getTargetOutputAbsRad();
  float current_output_raw = GripperAPI::motorMTToOutputRawRad(pos_mt);

  // Hard limit: never command further outside the allowed range.
  if (current_output_raw > GRIP_OUTPUT_MAX_RAD && target_output_abs > current_output_raw) {
    target_output_abs = current_output_raw;
  } else if (current_output_raw < 0.0f && target_output_abs < current_output_raw) {
    target_output_abs = current_output_raw;
  }

  float motor_target_mt = GripperAPI::outputToMotorMT(target_output_abs, pos_mt);

  // Outer position -> inner velocity command
  float vel_cmd = pvc.compute(motor_target_mt, pos_mt);

  // Apply velocity command
  motor.move(vel_cmd);
}
