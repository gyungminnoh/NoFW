#include <Arduino.h>

#include "board_config.h"
#include "can_transport.h"
#include "tmag_self_test.h"

namespace {

constexpr uint16_t kTmagSummaryCanId = 0x5D0 + CAN_NODE_ID;
constexpr uint16_t kTmagConfigCanId = 0x5E0 + CAN_NODE_ID;
constexpr uint16_t kTmagDiagCanId = 0x5F0 + CAN_NODE_ID;
constexpr uint32_t kDiagPeriodMs = 500;

TmagSelfTest::Result g_result;

void deselectAllSpiSlaves() {
  pinMode(PIN_AS5048_CS, OUTPUT);
  digitalWrite(PIN_AS5048_CS, HIGH);
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);
  pinMode(PIN_TMAG5170_CS, OUTPUT);
  digitalWrite(PIN_TMAG5170_CS, HIGH);
}

void sendSummaryFrame() {
  uint8_t data[8] = {0};
  data[0] = 0xD6;
  data[1] = static_cast<uint8_t>(g_result.status);
  data[2] = g_result.failed_step;
  data[3] = 0;
  data[4] = 0;
  data[5] = 0;
  data[6] = 0;
  data[7] = 0;
  CanTransport::sendStd(kTmagSummaryCanId, data, 8);
}

void sendConfigFrame() {
  uint8_t data[8] = {0};
  data[0] = 0xD7;
  data[1] = static_cast<uint8_t>(g_result.test_config_after_crc_off & 0xFFu);
  data[2] = static_cast<uint8_t>((g_result.test_config_after_crc_off >> 8) & 0xFFu);
  data[3] = static_cast<uint8_t>(g_result.test_config_after_write & 0xFFu);
  data[4] = static_cast<uint8_t>((g_result.test_config_after_write >> 8) & 0xFFu);
  data[5] = static_cast<uint8_t>(g_result.afe_status_first & 0xFFu);
  data[6] = static_cast<uint8_t>((g_result.afe_status_first >> 8) & 0xFFu);
  data[7] = 0;
  CanTransport::sendStd(kTmagConfigCanId, data, 8);
}

void sendDiagFrame() {
  uint8_t data[8] = {0};
  data[0] = 0xD8;
  data[1] = 0;
  data[2] = 0;
  data[3] = static_cast<uint8_t>(g_result.status_after_crc_off & 0xFFu);
  data[4] = static_cast<uint8_t>((g_result.status_after_crc_off >> 8) & 0xFFu);
  data[5] = static_cast<uint8_t>(g_result.status_after_test_write & 0xFFu);
  data[6] = static_cast<uint8_t>((g_result.status_after_test_write >> 8) & 0xFFu);
  data[7] = static_cast<uint8_t>(g_result.disable_crc_response & 0xFFu);
  CanTransport::sendStd(kTmagDiagCanId, data, 8);
}

} // namespace

void setup() {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  deselectAllSpiSlaves();
  SPI.begin();
  CanTransport::begin1Mbps();
  g_result = TmagSelfTest::run();
}

void loop() {
  static uint32_t last_tx_ms = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_tx_ms) >= kDiagPeriodMs) {
    last_tx_ms = now;
    sendSummaryFrame();
    sendConfigFrame();
    sendDiagFrame();
  }
}
