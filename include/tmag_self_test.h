#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace TmagSelfTest {

enum class Status : uint8_t {
  Failed = 0,
  Passed = 1,
};

struct Result {
  Status status = Status::Failed;
  uint8_t failed_step = 0;
  uint16_t test_config_after_crc_off = 0;
  uint16_t afe_status_first = 0;
  uint16_t test_config_after_write = 0;
  uint32_t disable_crc_response = 0;
  uint16_t status_after_crc_off = 0;
  uint16_t status_after_test_write = 0;
};

Result run(SPIClass& spi = SPI);

} // namespace TmagSelfTest
