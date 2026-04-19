#include <Arduino.h>
#include <SPI.h>
#include <SimpleFOC.h>
#include <STM32_CAN.h>

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

STM32_CAN Can1(CAN1, DEF);

enum class ControlMode {
  Velocity,
  Position,
  OpenLoop,
};

static constexpr uint16_t kMotorCmdId = 0x210 + CAN_NODE_ID;
static constexpr uint16_t kMotorStatusId = 0x410 + CAN_NODE_ID;
static constexpr uint32_t kStatusPeriodMs = 200;

enum MotorCmd : uint8_t {
  MT_CMD_ENABLE = 0x01,
  MT_CMD_MODE   = 0x02,
  MT_CMD_TARGET = 0x03,
  MT_CMD_VLIMIT = 0x04,
  MT_CMD_STOP   = 0x05,
  MT_CMD_STREAM = 0x06,
  MT_CMD_STATUS = 0x07,
};

static ControlMode mode = ControlMode::Velocity;
static float target = 0.0f;
static float voltage_limit = 2.0f;
static bool motor_enabled = false;
static bool stream_status = false;
static bool motor_inited = false;

static void setGate(bool enable) {
  digitalWrite(PIN_EN_GATE, enable ? HIGH : LOW);
  motor_enabled = enable;
  if (motor_inited) {
    if (enable) {
      motor.enable();
    } else {
      motor.disable();
    }
  }
}

static void printHelp() {
  DebugSerial.println("=== Motor Test ===");
  DebugSerial.print("CAN cmd ID: 0x");
  DebugSerial.println(kMotorCmdId, HEX);
  DebugSerial.print("CAN status ID: 0x");
  DebugSerial.println(kMotorStatusId, HEX);
  DebugSerial.println("Cmd format: data[0]=CMD, payload little-endian");
  DebugSerial.println("CMD 0x01: Enable (data[1]=1 on, 0 off)");
  DebugSerial.println("CMD 0x02: Mode (data[1]=0 vel, 1 pos, 2 open)");
  DebugSerial.println("CMD 0x03: Target (int16 centi-rad or centi-rad/s)");
  DebugSerial.println("CMD 0x04: Vlimit (uint16 centi-volt)");
  DebugSerial.println("CMD 0x05: Stop (target=0)");
  DebugSerial.println("CMD 0x06: Stream (data[1]=1 on, 0 off)");
  DebugSerial.println("CMD 0x07: Status once (no payload)");
}

static void setMode(ControlMode new_mode) {
  mode = new_mode;
  switch (mode) {
    case ControlMode::Velocity:
      motor.controller = MotionControlType::velocity;
      break;
    case ControlMode::Position:
      motor.controller = MotionControlType::angle;
      break;
    case ControlMode::OpenLoop:
      motor.controller = MotionControlType::velocity_openloop;
      break;
  }
  target = 0.0f;
}

static int16_t readS16LE(const uint8_t* data) {
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint16_t readU16LE(const uint8_t* data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void sendStatusFrame() {
  sensor.update();
  float shaft = sensor.getAngle();
  int16_t target_c = (int16_t)lroundf(target * 100.0f);
  int16_t shaft_c = (int16_t)lroundf(shaft * 100.0f);
  uint16_t vlim_c = (uint16_t)lroundf(voltage_limit * 100.0f);

  CAN_message_t msg;
  msg.id = kMotorStatusId;
  msg.len = 8;
  msg.flags.extended = 0;
  msg.flags.remote = 0;
  msg.buf[0] = (uint8_t)mode;
  msg.buf[1] = motor_enabled ? 1 : 0;
  msg.buf[2] = (uint8_t)(target_c & 0xFF);
  msg.buf[3] = (uint8_t)((target_c >> 8) & 0xFF);
  msg.buf[4] = (uint8_t)(shaft_c & 0xFF);
  msg.buf[5] = (uint8_t)((shaft_c >> 8) & 0xFF);
  msg.buf[6] = (uint8_t)(vlim_c & 0xFF);
  msg.buf[7] = (uint8_t)((vlim_c >> 8) & 0xFF);
  Can1.write(msg);
}

static void handleCanMessage(const CAN_message_t& msg) {
  if (msg.flags.extended) return;
  if ((uint16_t)msg.id != kMotorCmdId) return;
  if (msg.len < 1) return;

  const uint8_t cmd = msg.buf[0];
  switch (cmd) {
    case MT_CMD_ENABLE: {
      if (msg.len < 2) return;
      setGate(msg.buf[1] != 0);
      DebugSerial.println(motor_enabled ? "gate ON" : "gate OFF");
      break;
    }
    case MT_CMD_MODE: {
      if (msg.len < 2) return;
      uint8_t m = msg.buf[1];
      if (m == 0) {
        setMode(ControlMode::Velocity);
        DebugSerial.println("mode=velocity");
      } else if (m == 1) {
        setMode(ControlMode::Position);
        DebugSerial.println("mode=position");
      } else if (m == 2) {
        setMode(ControlMode::OpenLoop);
        DebugSerial.println("mode=openloop");
      }
      break;
    }
    case MT_CMD_TARGET: {
      if (msg.len < 3) return;
      int16_t v = readS16LE(&msg.buf[1]);
      target = ((float)v) * 0.01f;
      DebugSerial.print("target=");
      DebugSerial.println(target, 4);
      break;
    }
    case MT_CMD_VLIMIT: {
      if (msg.len < 3) return;
      uint16_t v = readU16LE(&msg.buf[1]);
      voltage_limit = ((float)v) * 0.01f;
      driver.voltage_limit = voltage_limit;
      motor.voltage_limit = voltage_limit;
      DebugSerial.print("vlim=");
      DebugSerial.println(voltage_limit, 2);
      break;
    }
    case MT_CMD_STOP: {
      target = 0.0f;
      DebugSerial.println("target=0");
      break;
    }
    case MT_CMD_STREAM: {
      if (msg.len < 2) return;
      stream_status = (msg.buf[1] != 0);
      DebugSerial.println(stream_status ? "stream ON" : "stream OFF");
      break;
    }
    case MT_CMD_STATUS: {
      sendStatusFrame();
      break;
    }
    default:
      break;
  }
}

static void initPins() {
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
}

void setup() {
  DebugSerial.setRx(PIN_UART_RX);
  DebugSerial.setTx(PIN_UART_TX);
  DebugSerial.begin(115200);
  delay(200);

  initPins();

  Can1.begin();
  Can1.setBaudRate(CAN_BITRATE);

  SPI.begin();
  sensor.init();

  driver.voltage_power_supply = BUS_VOLTAGE;
  driver.voltage_limit = voltage_limit;
  driver.init();

  motor.linkDriver(&driver);
  motor.linkSensor(&sensor);
  motor.torque_controller = TorqueControlType::voltage;
  motor.controller = MotionControlType::velocity;

  motor.voltage_sensor_align = ALIGN_VOLTAGE;
  motor.voltage_limit = voltage_limit;

  motor.PID_velocity.P = 1.0f;
  motor.PID_velocity.I = 0.0f;
  motor.PID_velocity.D = 0.0f;
  motor.LPF_velocity.Tf = 0.01f;
  motor.P_angle.P = 1.0f;
  motor.velocity_limit = 20.0f;

  digitalWrite(PIN_EN_GATE, HIGH);
  motor_enabled = true;
  delay(10);
  motor.init();
  int foc_ok = motor.initFOC();
  motor_inited = true;
  motor.enable();

  DebugSerial.println();
  DebugSerial.println("Motor test ready.");
  DebugSerial.print("initFOC=");
  DebugSerial.println(foc_ok ? "OK" : "FAIL");
  printHelp();
}

void loop() {
  CAN_message_t msg;
  while (Can1.read(msg)) {
    handleCanMessage(msg);
  }

  if (motor_enabled) {
    if (mode != ControlMode::OpenLoop) {
      motor.loopFOC();
    }
    motor.move(target);
  }

  if (stream_status) {
    static uint32_t last_ms = 0;
    if ((uint32_t)(millis() - last_ms) >= kStatusPeriodMs) {
      last_ms = millis();
      sendStatusFrame();
    }
  }
}
