#include <Arduino.h>
#include <SPI.h>
#include <SimpleFOC.h>

#include "board_config.h"
#include "as5048a_custom_sensor.h"

extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  // 16MHz crystal
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
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

HardwareSerial DebugSerial(USART2);

AS5048A_CustomSensor sensor(PIN_AS5048_CS, SPI);
BLDCMotor motor(POLE_PAIRS);
BLDCDriver3PWM driver(PIN_PWM_A, PIN_PWM_B, PIN_PWM_C);

namespace {
constexpr float kTargetStepRadS = 10.0f;
constexpr float kTargetMaxRadS = 8000.0f;
constexpr uint16_t kDebounceMs = 30;
constexpr uint16_t kPrintPeriodMs = 200;

struct DebouncedButton {
  bool stable_pressed = false;
  bool last_raw = false;
  uint32_t last_change_ms = 0;

  bool update(bool raw_pressed, uint32_t now_ms) {
    if (raw_pressed != last_raw) {
      last_raw = raw_pressed;
      last_change_ms = now_ms;
    }

    if ((uint32_t)(now_ms - last_change_ms) >= kDebounceMs &&
        raw_pressed != stable_pressed) {
      stable_pressed = raw_pressed;
      if (stable_pressed) {
        return true;
      }
    }
    return false;
  }
};

float target_velocity = 0.0f;
bool foc_ready = false;
DebouncedButton user_button;

void initPins() {
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

  pinMode(PIN_USER_BTN, INPUT_PULLUP);
}
} // namespace

void setup() {
  DebugSerial.setRx(PIN_UART_RX);
  DebugSerial.setTx(PIN_UART_TX);
  DebugSerial.begin(115200);
  delay(200);

  initPins();

  SPI.begin();
  sensor.init();

  driver.voltage_power_supply = BUS_VOLTAGE;
  driver.voltage_limit = VOLTAGE_LIMIT;
  driver.init();

  motor.linkDriver(&driver);
  motor.linkSensor(&sensor);
  motor.torque_controller = TorqueControlType::voltage;
  motor.controller = MotionControlType::velocity;

  motor.voltage_sensor_align = ALIGN_VOLTAGE;
  motor.voltage_limit = TORQUE_LIMIT_VOLTS;

  motor.PID_velocity.P = 0.1f;
  motor.PID_velocity.I = 0.2f;
  motor.PID_velocity.D = 0.0f;
  motor.LPF_velocity.Tf = 0.01f;
  motor.velocity_limit = kTargetMaxRadS;

  digitalWrite(PIN_EN_GATE, HIGH);
  delay(10);
  motor.init();
  int foc_ok = motor.initFOC();
  foc_ready = (foc_ok != 0);
  motor.enable();

  DebugSerial.println();
  DebugSerial.println("=== Max Speed Tuner ===");
  DebugSerial.print("initFOC=");
  DebugSerial.println(foc_ready ? "OK" : "FAIL");
  DebugSerial.print("step=");
  DebugSerial.print(kTargetStepRadS, 2);
  DebugSerial.print(" rad/s, max=");
  DebugSerial.println(kTargetMaxRadS, 2);
  DebugSerial.println("Press USER button to increase target velocity.");
}

void loop() {
  const uint32_t now_ms = millis();

  const bool raw_pressed = (digitalRead(PIN_USER_BTN) == LOW);
  if (user_button.update(raw_pressed, now_ms)) {
    target_velocity += kTargetStepRadS;
    if (target_velocity > kTargetMaxRadS) {
      target_velocity = 0.0f;
    }
    DebugSerial.print("[BTN] target_velocity=");
    DebugSerial.println(target_velocity, 2);
  }

  if (foc_ready) {
    motor.loopFOC();
    motor.move(target_velocity);
  }

  static uint32_t last_print_ms = 0;
  if ((uint32_t)(now_ms - last_print_ms) >= kPrintPeriodMs) {
    last_print_ms = now_ms;
    const float actual_vel = motor.shaftVelocity();
    DebugSerial.print("target=");
    DebugSerial.print(target_velocity, 2);
    DebugSerial.print(" rad/s  actual=");
    DebugSerial.print(actual_vel, 2);
    DebugSerial.println(" rad/s");
  }
}
