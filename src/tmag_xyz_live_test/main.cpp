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
constexpr uint8_t kRegAfeStatus = 0x0D;
constexpr uint8_t kRegSysStatus = 0x0E;

constexpr TMAG5170::DeviceVariant kTmagVariant = TMAG5170::DeviceVariant::A2;

constexpr uint16_t kDeviceConfigActiveMeasure = 0x0028;
constexpr uint16_t kSensorConfigXYZEnable = 0x01C0;
constexpr uint16_t kSystemConfigDefault = 0x0000;

constexpr uint16_t kStatusCanId = 0x600 + CAN_NODE_ID;
constexpr uint16_t kMilliTeslaCanId = 0x610 + CAN_NODE_ID;
constexpr uint16_t kRawCanId = 0x620 + CAN_NODE_ID;
constexpr uint16_t kConfigCanId = 0x630 + CAN_NODE_ID;
constexpr uint16_t kXFrameCanId = 0x640 + CAN_NODE_ID;
constexpr uint16_t kYFrameCanId = 0x650 + CAN_NODE_ID;
constexpr uint16_t kZFrameCanId = 0x660 + CAN_NODE_ID;
constexpr uint16_t kDecodeCanId = 0x670 + CAN_NODE_ID;
constexpr uint32_t kDiagPeriodMs = 20;
constexpr uint32_t kDebugPeriodMs = 100;

uint8_t g_sample_counter = 0;

int16_t mtToCenti(float mt) {
  const long scaled = lroundf(mt * 100.0f);
  if (scaled > 32767L) return 32767;
  if (scaled < -32768L) return -32768;
  return static_cast<int16_t>(scaled);
}

int16_t signExtend12(uint16_t value) {
  value &= 0x0FFFu;
  if ((value & 0x0800u) != 0u) {
    value |= 0xF000u;
  }
  return static_cast<int16_t>(value);
}

void packU32Le(uint8_t* dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFu);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
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
  static uint32_t last_debug_ms = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_tx_ms) < kDiagPeriodMs) {
    return;
  }
  last_tx_ms = now;
  ++g_sample_counter;

  const TMAG5170::RegisterRead conv = TMAG5170::readRegister(kRegConvStatus);
  const TMAG5170::RegisterRead x = TMAG5170::readRegister(kRegXResult);
  const TMAG5170::RegisterRead y = TMAG5170::readRegister(kRegYResult);
  const TMAG5170::RegisterRead z = TMAG5170::readRegister(kRegZResult);
  const TMAG5170::RegisterRead afe = TMAG5170::readRegister(kRegAfeStatus);
  const TMAG5170::RegisterRead sys = TMAG5170::readRegister(kRegSysStatus);

  const int16_t x_raw = TMAG5170::dataToSigned(x.data);
  const int16_t y_raw = TMAG5170::dataToSigned(y.data);
  const int16_t z_raw = TMAG5170::dataToSigned(z.data);
  const int16_t x_msb12 = signExtend12(static_cast<uint16_t>(x.data >> 4));
  const int16_t y_msb12 = signExtend12(static_cast<uint16_t>(y.data >> 4));
  const int16_t z_msb12 = signExtend12(static_cast<uint16_t>(z.data >> 4));
  const uint8_t x_lsn = static_cast<uint8_t>(x.data & 0x0Fu);
  const uint8_t y_lsn = static_cast<uint8_t>(y.data & 0x0Fu);
  const uint8_t z_lsn = static_cast<uint8_t>(z.data & 0x0Fu);

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
    data[7] = g_sample_counter;
    CanTransport::sendStd(kStatusCanId, data, 8);
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
    CanTransport::sendStd(kMilliTeslaCanId, data, 8);
  }

  {
    uint8_t data[8] = {0};
    data[0] = 0xE8;
    data[1] = static_cast<uint8_t>(x_raw & 0xFFu);
    data[2] = static_cast<uint8_t>((x_raw >> 8) & 0xFFu);
    data[3] = static_cast<uint8_t>(y_raw & 0xFFu);
    data[4] = static_cast<uint8_t>((y_raw >> 8) & 0xFFu);
    data[5] = static_cast<uint8_t>(z_raw & 0xFFu);
    data[6] = static_cast<uint8_t>((z_raw >> 8) & 0xFFu);
    data[7] = g_sample_counter;
    CanTransport::sendStd(kRawCanId, data, 8);
  }

  if ((uint32_t)(now - last_debug_ms) >= kDebugPeriodMs) {
    last_debug_ms = now;

    const TMAG5170::RegisterRead device_cfg = TMAG5170::readRegister(kRegDeviceConfig);
    const TMAG5170::RegisterRead sensor_cfg = TMAG5170::readRegister(kRegSensorConfig);
    const TMAG5170::RegisterRead system_cfg = TMAG5170::readRegister(kRegSystemConfig);

    {
      uint8_t data[8] = {0};
      data[0] = 0xE9;
      data[1] = static_cast<uint8_t>(device_cfg.data & 0xFFu);
      data[2] = static_cast<uint8_t>((device_cfg.data >> 8) & 0xFFu);
      data[3] = static_cast<uint8_t>(sensor_cfg.data & 0xFFu);
      data[4] = static_cast<uint8_t>((sensor_cfg.data >> 8) & 0xFFu);
      data[5] = static_cast<uint8_t>(system_cfg.data & 0xFFu);
      data[6] = static_cast<uint8_t>((system_cfg.data >> 8) & 0xFFu);
      data[7] = static_cast<uint8_t>(
          ((device_cfg.data == kDeviceConfigActiveMeasure) ? 0x01u : 0x00u) |
          ((sensor_cfg.data == kSensorConfigXYZEnable) ? 0x02u : 0x00u) |
          ((system_cfg.data == kSystemConfigDefault) ? 0x04u : 0x00u) |
          (((conv.data & 0x2000u) != 0u) ? 0x08u : 0x00u));
      CanTransport::sendStd(kConfigCanId, data, 8);
    }

    {
      uint8_t data[8] = {0};
      data[0] = 0xEA;
      packU32Le(&data[1], x.raw);
      data[5] = static_cast<uint8_t>(x.status & 0xFFu);
      data[6] = static_cast<uint8_t>((x.status >> 8) & 0xFFu);
      data[7] = x_lsn;
      CanTransport::sendStd(kXFrameCanId, data, 8);
    }

    {
      uint8_t data[8] = {0};
      data[0] = 0xEB;
      packU32Le(&data[1], y.raw);
      data[5] = static_cast<uint8_t>(y.status & 0xFFu);
      data[6] = static_cast<uint8_t>((y.status >> 8) & 0xFFu);
      data[7] = y_lsn;
      CanTransport::sendStd(kYFrameCanId, data, 8);
    }

    {
      uint8_t data[8] = {0};
      data[0] = 0xEC;
      packU32Le(&data[1], z.raw);
      data[5] = static_cast<uint8_t>(z.status & 0xFFu);
      data[6] = static_cast<uint8_t>((z.status >> 8) & 0xFFu);
      data[7] = z_lsn;
      CanTransport::sendStd(kZFrameCanId, data, 8);
    }

    {
      uint8_t data[8] = {0};
      data[0] = 0xED;
      data[1] = static_cast<uint8_t>(x_msb12 & 0xFFu);
      data[2] = static_cast<uint8_t>((x_msb12 >> 8) & 0xFFu);
      data[3] = static_cast<uint8_t>(y_msb12 & 0xFFu);
      data[4] = static_cast<uint8_t>((y_msb12 >> 8) & 0xFFu);
      data[5] = static_cast<uint8_t>(z_msb12 & 0xFFu);
      data[6] = static_cast<uint8_t>((z_msb12 >> 8) & 0xFFu);
      data[7] = static_cast<uint8_t>((x_lsn & 0x03u) | ((y_lsn & 0x03u) << 2) | ((z_lsn & 0x03u) << 4));
      CanTransport::sendStd(kDecodeCanId, data, 8);
    }
  }
}
