#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SimpleFOC.h>

// ========================================================
// AS5048A: 2-frame read, SPI_MODE1, 1MHz
// Wrapped as SimpleFOC Sensor
// ========================================================
class AS5048A_CustomSensor : public Sensor {
 public:
  AS5048A_CustomSensor(uint8_t cs_pin, SPIClass& spi = SPI)
    : cs_(cs_pin), spiBus_(spi) {}

  void init() override {
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    spiBus_.begin();
  }

 protected:
  float getSensorAngle() override {
    const uint16_t raw = readAngleRaw_();        // 0..16383
    float angle = (raw * _2PI) / 16384.0f;       // rad
    if (AS5048A_INVERT) {
      angle = _2PI - angle;
      if (angle >= _2PI) angle -= _2PI;
    }
    return angle;
  }

 private:
  uint8_t cs_;
  SPIClass& spiBus_;

  static inline uint8_t evenParity15_(uint16_t x) {
    x &= 0x7FFF;
    uint8_t p = 0;
    while (x) { p ^= 1; x &= (x - 1); }
    return p; // 1이면 홀수 → parity bit 세팅 필요
  }

  static inline uint16_t makeReadCmd_(uint16_t addr14) {
    uint16_t cmd = (1u << 14) | (addr14 & 0x3FFF); // bit14=READ
    if (evenParity15_(cmd)) cmd |= 0x8000;         // bit15=parity
    return cmd;
  }

  inline uint16_t spiTransfer16_(uint16_t w) {
    uint8_t hi = (w >> 8) & 0xFF;
    uint8_t lo = w & 0xFF;
    uint8_t rhi = spiBus_.transfer(hi);
    uint8_t rlo = spiBus_.transfer(lo);
    return (uint16_t(rhi) << 8) | rlo;
  }

  uint16_t readAngleRaw_() {
    const uint16_t READ_ANGLE = makeReadCmd_(0x3FFF);

    spiBus_.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));

    // 1) command frame
    digitalWrite(cs_, LOW);
    (void)spiTransfer16_(READ_ANGLE);
    digitalWrite(cs_, HIGH);
    delayMicroseconds(2);

    // 2) NOP frame receives actual data
    digitalWrite(cs_, LOW);
    uint16_t resp = spiTransfer16_(0x0000);
    digitalWrite(cs_, HIGH);

    spiBus_.endTransaction();

    return resp & 0x3FFF;
  }
};
