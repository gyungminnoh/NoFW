#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace FM25CL64B {

constexpr uint16_t kCapacityBytes = 8192;

void begin(SPIClass& spi = SPI);
uint8_t readStatusRegister(SPIClass& spi = SPI);
void writeEnable(SPIClass& spi = SPI);
void writeStatusRegister(uint8_t value, SPIClass& spi = SPI);
bool readBytes(uint16_t address, void* dst, size_t len, SPIClass& spi = SPI);
bool writeBytes(uint16_t address, const void* src, size_t len, SPIClass& spi = SPI);

template <typename T>
bool readObject(uint16_t address, T& out, SPIClass& spi = SPI) {
  return readBytes(address, &out, sizeof(T), spi);
}

template <typename T>
bool writeObject(uint16_t address, const T& in, SPIClass& spi = SPI) {
  return writeBytes(address, &in, sizeof(T), spi);
}

} // namespace FM25CL64B
