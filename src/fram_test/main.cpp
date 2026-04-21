#include <Arduino.h>

#include "board_config.h"
#include "can_transport.h"
#include "fm25cl64b_fram.h"
#include "fram_self_test.h"

namespace {

constexpr uint16_t kFramDiagCanId = 0x5A0 + CAN_NODE_ID;
constexpr uint16_t kFramDiagLowLevelCanId = 0x5B0 + CAN_NODE_ID;
constexpr uint16_t kFramDiagStatusCanId = 0x5C0 + CAN_NODE_ID;
constexpr uint32_t kDiagPeriodMs = 500;

FramSelfTest::Result g_result;

void deselectAllSpiSlaves() {
  pinMode(PIN_AS5048_CS, OUTPUT);
  digitalWrite(PIN_AS5048_CS, HIGH);
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);
  pinMode(PIN_SPI1_NCS_3, OUTPUT);
  digitalWrite(PIN_SPI1_NCS_3, HIGH);
}

void sendSummaryFrame() {
  uint8_t data[8] = {0};
  data[0] = 0xF6;
  data[1] = static_cast<uint8_t>(g_result.status);
  data[2] = g_result.failed_step;
  data[3] = 0;
  data[4] = static_cast<uint8_t>(g_result.failed_address & 0xFF);
  data[5] = static_cast<uint8_t>((g_result.failed_address >> 8) & 0xFF);
  data[6] = 0;
  data[7] = 0;
  CanTransport::sendStd(kFramDiagCanId, data, 8);
}

void sendLowLevelFrame() {
  uint8_t data[8] = {0};
  data[0] = 0xF7;
  data[1] = g_result.status_before_wren;
  data[2] = g_result.status_after_wren;
  data[3] = g_result.status_after_write;
  data[4] = g_result.probe_original;
  data[5] = g_result.probe_expected;
  data[6] = g_result.probe_readback;
  data[7] = 0;
  CanTransport::sendStd(kFramDiagLowLevelCanId, data, 8);
}

void sendStatusFrame() {
  uint8_t data[8] = {0};
  data[0] = 0xF8;
  data[1] = g_result.status_before_wrsr;
  data[2] = g_result.status_after_wrsr;
  data[3] = g_result.status_restored;
  data[4] = 0;
  data[5] = 0;
  data[6] = 0;
  data[7] = 0;
  CanTransport::sendStd(kFramDiagStatusCanId, data, 8);
}

} // namespace

void setup() {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  deselectAllSpiSlaves();
  SPI.begin();
  FM25CL64B::begin();
  CanTransport::begin1Mbps();
  g_result = FramSelfTest::run();
}

void loop() {
  static uint32_t last_tx_ms = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_tx_ms) >= kDiagPeriodMs) {
    last_tx_ms = now;
    sendSummaryFrame();
    sendLowLevelFrame();
    sendStatusFrame();
  }
}
