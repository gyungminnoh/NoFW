#include <Arduino.h>
#include <SPI.h>

#include "board_config.h"
#include "can_transport.h"
#include "fm25cl64b_fram.h"

namespace {

constexpr uint16_t kStatusCanId = 0x5E7;
constexpr uint8_t kChunkSize = 32;

void sendStatus(uint8_t code, uint16_t address, uint16_t detail) {
  uint8_t data[8] = {
      0xFE,
      code,
      static_cast<uint8_t>(address & 0xFF),
      static_cast<uint8_t>((address >> 8) & 0xFF),
      static_cast<uint8_t>(detail & 0xFF),
      static_cast<uint8_t>((detail >> 8) & 0xFF),
      0,
      0,
  };
  CanTransport::sendStd(kStatusCanId, data, sizeof(data));
}

bool eraseFram() {
  uint8_t zeros[kChunkSize] = {};
  for (uint16_t address = 0; address < FM25CL64B::kCapacityBytes; address += kChunkSize) {
    if (!FM25CL64B::writeBytes(address, zeros, sizeof(zeros))) {
      sendStatus(0xE1, address, sizeof(zeros));
      return false;
    }
  }

  uint8_t verify[kChunkSize] = {};
  for (uint16_t address = 0; address < FM25CL64B::kCapacityBytes; address += kChunkSize) {
    if (!FM25CL64B::readBytes(address, verify, sizeof(verify))) {
      sendStatus(0xE2, address, sizeof(verify));
      return false;
    }
    for (uint8_t i = 0; i < sizeof(verify); ++i) {
      if (verify[i] != 0) {
        sendStatus(0xE3, address + i, verify[i]);
        return false;
      }
    }
  }

  sendStatus(0x01, FM25CL64B::kCapacityBytes, 0);
  return true;
}

}  // namespace

void setup() {
  pinMode(PIN_EN_GATE, OUTPUT);
  digitalWrite(PIN_EN_GATE, LOW);
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);

  SPI.begin();
  FM25CL64B::begin();
  CanTransport::begin1Mbps();

  delay(100);
  sendStatus(0x00, 0, 0);
  eraseFram();
}

void loop() {
  sendStatus(0x7F, FM25CL64B::kCapacityBytes, 0);
  delay(500);
}
