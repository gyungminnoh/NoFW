#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace TMAG5170 {

enum class DeviceVariant : uint8_t {
  A1 = 0,
  A2 = 1,
};

struct RegisterRead {
  uint16_t data = 0;
  uint16_t status = 0;
  uint32_t raw = 0;
};

void begin(SPIClass& spi = SPI);
uint32_t transferFrame(uint32_t tx_frame, SPIClass& spi = SPI);
uint32_t disableCrc(SPIClass& spi = SPI);
bool writeRegister(uint8_t address, uint16_t value, SPIClass& spi = SPI);
bool writeRegister(uint8_t address, uint16_t value, uint8_t cmd_nibble, SPIClass& spi = SPI);
RegisterRead readRegister(uint8_t address, SPIClass& spi = SPI);
RegisterRead readRegister(uint8_t address, uint8_t cmd_nibble, SPIClass& spi = SPI);
int16_t dataToSigned(uint16_t raw_data);
float rawToMilliTesla(int16_t signed_code, float range_mT);
float rangeMilliTesla(uint16_t sensor_config, uint8_t axis_idx, DeviceVariant variant);

} // namespace TMAG5170
