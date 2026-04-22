#include <Arduino.h>

#include "board_config.h"
#include "can_transport.h"
#include "fm25cl64b_fram.h"
#include "fram_self_test.h"
#include "storage/config_store.h"

namespace {

constexpr uint16_t kFramDiagCanId = 0x5A0 + CAN_NODE_ID;
constexpr uint16_t kFramDiagLowLevelCanId = 0x5B0 + CAN_NODE_ID;
constexpr uint16_t kFramDiagStatusCanId = 0x5C0 + CAN_NODE_ID;
constexpr uint16_t kFramStoredBundleCanId = 0x5D0 + CAN_NODE_ID;
constexpr uint32_t kDiagPeriodMs = 500;

FramSelfTest::Result g_result;
ConfigStore::CalibrationBundle g_bundle = {};
bool g_bundle_loaded = false;

int16_t radToCdeg(float rad) {
  const float deg = rad * (18000.0f / PI);
  if (deg > 32767.0f) return 32767;
  if (deg < -32768.0f) return -32768;
  return static_cast<int16_t>(deg);
}

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

void sendStoredBundleFrame() {
  const int16_t tmag_cal_rms_cdeg =
      g_bundle.tmag.valid ? radToCdeg(g_bundle.tmag.calibration_rms_rad) : 0;

  uint8_t data[8] = {0};
  data[0] = 0xF9;
  data[1] = g_bundle_loaded ? 1 : 0;
  data[2] = g_bundle.foc.valid ? 1 : 0;
  data[3] = g_bundle.as5600.valid ? 1 : 0;
  data[4] = g_bundle.tmag.valid ? 1 : 0;
  data[5] = static_cast<uint8_t>(g_bundle.tmag.valid_bin_count & 0xFFu);
  data[6] = static_cast<uint8_t>((g_bundle.tmag.valid_bin_count >> 8) & 0xFFu);
  data[7] = static_cast<uint8_t>(tmag_cal_rms_cdeg & 0xFFu);
  CanTransport::sendStd(kFramStoredBundleCanId, data, 8);
}

} // namespace

void setup() {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  deselectAllSpiSlaves();
  SPI.begin();
  FM25CL64B::begin();
  CanTransport::begin1Mbps();
  g_result = FramSelfTest::run();
  g_bundle_loaded = ConfigStore::loadCalibrationBundleCompat(g_bundle);
}

void loop() {
  static uint32_t last_tx_ms = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_tx_ms) >= kDiagPeriodMs) {
    last_tx_ms = now;
    sendSummaryFrame();
    sendLowLevelFrame();
    sendStatusFrame();
    sendStoredBundleFrame();
  }
}
