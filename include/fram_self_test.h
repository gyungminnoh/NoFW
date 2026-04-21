#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace FramSelfTest {

enum class Status : uint8_t {
  Failed = 0,
  NeedsPowerCycle = 1,
  Passed = 2,
};

struct Result {
  Status status = Status::Failed;
  uint8_t failed_step = 0;
  uint16_t failed_address = 0;
  uint8_t status_before_wren = 0;
  uint8_t status_after_wren = 0;
  uint8_t status_before_wrsr = 0;
  uint8_t status_after_wrsr = 0;
  uint8_t status_restored = 0;
  uint8_t status_after_write = 0;
  uint8_t probe_original = 0;
  uint8_t probe_expected = 0;
  uint8_t probe_readback = 0;
};

Result run(SPIClass& spi = SPI);

} // namespace FramSelfTest
