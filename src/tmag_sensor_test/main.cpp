#include <Arduino.h>

#include "board_config.h"
#include "can_transport.h"
#include "tmag5170_spi.h"

namespace {

constexpr uint8_t kRegDeviceConfig = 0x00;
constexpr uint8_t kRegSensorConfig = 0x01;
constexpr uint8_t kRegSystemConfig = 0x02;
constexpr uint8_t kRegConvStatus = 0x08;
constexpr uint8_t kRegXResult = 0x09;
constexpr uint8_t kRegYResult = 0x0A;
constexpr uint8_t kRegZResult = 0x0B;
constexpr uint8_t kRegTempResult = 0x0C;
constexpr uint8_t kRegAfeStatus = 0x0D;
constexpr uint8_t kRegSysStatus = 0x0E;
constexpr TMAG5170::DeviceVariant kTmagVariant = TMAG5170::DeviceVariant::A2;

constexpr uint16_t kTmagLiveStatusCanId = 0x600 + CAN_NODE_ID;
constexpr uint16_t kTmagLiveAxisCanId = 0x610 + CAN_NODE_ID;
constexpr uint16_t kTmagLiveAuxCanId = 0x620 + CAN_NODE_ID;
constexpr uint32_t kDiagPeriodMs = 100;

// DEVICE_CONFIG:
// - OPERATING_MODE = 010b (active measure mode, continuous conversion)
// - T_CH_EN = 1 (include temperature conversions)
constexpr uint16_t kDeviceConfigActiveMeasure = 0x0028;

// SENSOR_CONFIG:
// - MAG_CH_EN = 0111b (enable X, Y, Z)
// - X/Y/Z range = default
constexpr uint16_t kSensorConfigXYZEnable = 0x01C0;

// SYSTEM_CONFIG:
// - DATA_TYPE = 0 (default 32-bit register access)
// - TRIGGER_MODE = 0 (not used in active measure mode)
constexpr uint16_t kSystemConfigDefault = 0x0000;

int16_t mtToCenti(float mt) {
  const long scaled = lroundf(mt * 100.0f);
  if (scaled > 32767L) return 32767;
  if (scaled < -32768L) return -32768;
  return static_cast<int16_t>(scaled);
}

void deselectAllSpiSlaves() {
  pinMode(PIN_AS5048_CS, OUTPUT);
  digitalWrite(PIN_AS5048_CS, HIGH);
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);
  pinMode(PIN_TMAG5170_CS, OUTPUT);
  digitalWrite(PIN_TMAG5170_CS, HIGH);
}

} // namespace

void setup() {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  deselectAllSpiSlaves();
  SPI.begin();
  TMAG5170::begin();
  TMAG5170::disableCrc();
  TMAG5170::writeRegister(kRegDeviceConfig, 0x0000);
  TMAG5170::writeRegister(kRegSensorConfig, kSensorConfigXYZEnable);
  TMAG5170::writeRegister(kRegSystemConfig, kSystemConfigDefault);
  TMAG5170::writeRegister(kRegDeviceConfig, kDeviceConfigActiveMeasure);
  delay(20);
  CanTransport::begin1Mbps();
}

void loop() {
  static uint32_t last_tx_ms = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_tx_ms) < kDiagPeriodMs) {
    return;
  }
  last_tx_ms = now;

  const TMAG5170::RegisterRead conv = TMAG5170::readRegister(kRegConvStatus);
  const TMAG5170::RegisterRead x = TMAG5170::readRegister(kRegXResult);
  const TMAG5170::RegisterRead y = TMAG5170::readRegister(kRegYResult);
  const TMAG5170::RegisterRead z = TMAG5170::readRegister(kRegZResult);
  const TMAG5170::RegisterRead temp = TMAG5170::readRegister(kRegTempResult);
  const TMAG5170::RegisterRead afe = TMAG5170::readRegister(kRegAfeStatus);
  const TMAG5170::RegisterRead sys = TMAG5170::readRegister(kRegSysStatus);

  const int16_t x_raw = TMAG5170::dataToSigned(x.data);
  const int16_t y_raw = TMAG5170::dataToSigned(y.data);
  const int16_t z_raw = TMAG5170::dataToSigned(z.data);
  const int16_t x_cmt = mtToCenti(
      TMAG5170::rawToMilliTesla(x_raw, TMAG5170::rangeMilliTesla(kSensorConfigXYZEnable, 0, kTmagVariant)));
  const int16_t y_cmt = mtToCenti(
      TMAG5170::rawToMilliTesla(y_raw, TMAG5170::rangeMilliTesla(kSensorConfigXYZEnable, 1, kTmagVariant)));
  const int16_t z_cmt = mtToCenti(
      TMAG5170::rawToMilliTesla(z_raw, TMAG5170::rangeMilliTesla(kSensorConfigXYZEnable, 2, kTmagVariant)));

  {
    uint8_t data[8] = {0};
    data[0] = 0xE6;
    data[1] = static_cast<uint8_t>(conv.data & 0xFFu);
    data[2] = static_cast<uint8_t>((conv.data >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(afe.data & 0xFFu);
    data[4] = static_cast<uint8_t>((afe.data >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(sys.data & 0xFFu);
    data[6] = static_cast<uint8_t>((sys.data >> 8) & 0xFFu);
    data[7] = 0;
    CanTransport::sendStd(kTmagLiveStatusCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    data[0] = 0xE7;
    data[1] = static_cast<uint8_t>(x_cmt & 0xFFu);
    data[2] = static_cast<uint8_t>((x_cmt >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(y_cmt & 0xFFu);
    data[4] = static_cast<uint8_t>((y_cmt >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(z_cmt & 0xFFu);
    data[6] = static_cast<uint8_t>((z_cmt >> 8) & 0xFFu);
    data[7] = static_cast<uint8_t>(kTmagVariant == TMAG5170::DeviceVariant::A2 ? 2 : 1);
    CanTransport::sendStd(kTmagLiveAxisCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    data[0] = 0xE8;
    data[1] = static_cast<uint8_t>(temp.data & 0xFFu);
    data[2] = static_cast<uint8_t>((temp.data >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(x_raw & 0xFFu);
    data[4] = static_cast<uint8_t>((x_raw >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(y_raw & 0xFFu);
    data[6] = static_cast<uint8_t>((y_raw >> 8) & 0xFFu);
    data[7] = static_cast<uint8_t>(z_raw & 0xFFu);
    CanTransport::sendStd(kTmagLiveAuxCanId, data, 8);
  }
}
