#include "tmag_self_test.h"

#include "tmag5170_spi.h"

namespace {

constexpr uint8_t kRegAfeStatus = 0x0D;
constexpr uint8_t kRegTestConfig = 0x0F;

constexpr uint16_t kMaskCrcDisable = 0x0004;
constexpr uint16_t kTestWriteValue = 0x0004;

TmagSelfTest::Result fail_(uint8_t step, const TmagSelfTest::Result& base) {
  TmagSelfTest::Result out = base;
  out.status = TmagSelfTest::Status::Failed;
  out.failed_step = step;
  return out;
}

} // namespace

namespace TmagSelfTest {

Result run(SPIClass& spi) {
  Result result = {};
  TMAG5170::begin(spi);

  result.disable_crc_response = TMAG5170::disableCrc(spi);

  const TMAG5170::RegisterRead test_after_crc = TMAG5170::readRegister(kRegTestConfig, spi);
  result.test_config_after_crc_off = test_after_crc.data;
  result.status_after_crc_off = test_after_crc.status;
  if ((test_after_crc.data & kMaskCrcDisable) == 0) {
    return fail_(1, result);
  }

  const TMAG5170::RegisterRead afe_first = TMAG5170::readRegister(kRegAfeStatus, spi);
  result.afe_status_first = afe_first.data;

  TMAG5170::writeRegister(kRegTestConfig, kTestWriteValue, spi);

  const TMAG5170::RegisterRead test_after_write = TMAG5170::readRegister(kRegTestConfig, spi);
  result.test_config_after_write = test_after_write.data;
  result.status_after_test_write = test_after_write.status;
  if ((test_after_write.data & kMaskCrcDisable) == 0) {
    return fail_(2, result);
  }

  result.status = Status::Passed;
  result.failed_step = 0;
  return result;
}

} // namespace TmagSelfTest
