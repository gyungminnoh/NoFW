#include "fram_self_test.h"

#include "fm25cl64b_fram.h"

namespace {

constexpr uint16_t kHeaderAddr = FM25CL64B::kCapacityBytes - 128;
constexpr uint16_t kPatternAddr = kHeaderAddr + 16;
constexpr uint16_t kPatternLen = 96;
constexpr uint32_t kPendingMagic = 0x46524D54UL; // "FRMT"
constexpr uint8_t kStatusWritableMask = 0x0C;

struct PendingHeader {
  uint32_t magic;
  uint16_t length;
  uint16_t checksum;
  uint16_t reserved0;
  uint16_t reserved1;
  uint16_t reserved2;
};

uint8_t patternByte_(uint16_t index) {
  return static_cast<uint8_t>((index * 37u + 11u) & 0xFFu);
}

uint16_t checksum_(const uint8_t* data, uint16_t len) {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < len; ++i) {
    sum = static_cast<uint16_t>((sum + data[i]) & 0xFFFFu);
  }
  return sum;
}

FramSelfTest::Result fail_(uint8_t step, uint16_t address) {
  FramSelfTest::Result out;
  out.status = FramSelfTest::Status::Failed;
  out.failed_step = step;
  out.failed_address = address;
  return out;
}

FramSelfTest::Result failWithDiag_(const FramSelfTest::Result& base, uint8_t step, uint16_t address) {
  FramSelfTest::Result out = base;
  out.status = FramSelfTest::Status::Failed;
  out.failed_step = step;
  out.failed_address = address;
  return out;
}

} // namespace

namespace FramSelfTest {

Result run(SPIClass& spi) {
  FM25CL64B::begin(spi);
  Result diag = {};

  uint8_t pattern[kPatternLen] = {};
  uint8_t verify[kPatternLen] = {};
  for (uint16_t i = 0; i < kPatternLen; ++i) {
    pattern[i] = patternByte_(i);
  }

  uint8_t overflow_probe[8] = {};
  if (FM25CL64B::readBytes(FM25CL64B::kCapacityBytes - 4, overflow_probe, sizeof(overflow_probe), spi)) {
    return fail_(1, FM25CL64B::kCapacityBytes - 4);
  }
  if (FM25CL64B::writeBytes(FM25CL64B::kCapacityBytes - 4, overflow_probe, sizeof(overflow_probe), spi)) {
    return fail_(2, FM25CL64B::kCapacityBytes - 4);
  }

  diag.status_before_wren = FM25CL64B::readStatusRegister(spi);
  FM25CL64B::writeEnable(spi);
  diag.status_after_wren = FM25CL64B::readStatusRegister(spi);
  diag.status_before_wrsr = diag.status_before_wren & kStatusWritableMask;

  const uint8_t toggled_status = static_cast<uint8_t>((diag.status_before_wren ^ kStatusWritableMask) & kStatusWritableMask);
  FM25CL64B::writeStatusRegister(toggled_status, spi);
  diag.status_after_wrsr = static_cast<uint8_t>(FM25CL64B::readStatusRegister(spi) & kStatusWritableMask);
  FM25CL64B::writeStatusRegister(diag.status_before_wren & kStatusWritableMask, spi);
  diag.status_restored = static_cast<uint8_t>(FM25CL64B::readStatusRegister(spi) & kStatusWritableMask);

  if (!FM25CL64B::readBytes(kPatternAddr + 1, &diag.probe_original, 1, spi)) {
    return failWithDiag_(diag, 15, static_cast<uint16_t>(kPatternAddr + 1));
  }
  diag.probe_expected = patternByte_(1);

  PendingHeader header = {};
  if (!FM25CL64B::readObject(kHeaderAddr, header, spi)) {
    return failWithDiag_(diag, 3, kHeaderAddr);
  }

  if (header.magic == kPendingMagic) {
    if (header.length != kPatternLen) {
      return failWithDiag_(diag, 4, kHeaderAddr);
    }

    if (!FM25CL64B::readBytes(kPatternAddr, verify, kPatternLen, spi)) {
      return failWithDiag_(diag, 5, kPatternAddr);
    }

    const uint16_t sum = checksum_(verify, kPatternLen);
    if (sum != header.checksum) {
      return failWithDiag_(diag, 6, kPatternAddr);
    }

    for (uint16_t i = 0; i < kPatternLen; ++i) {
      if (verify[i] != pattern[i]) {
        return failWithDiag_(diag, 7, static_cast<uint16_t>(kPatternAddr + i));
      }
    }

    PendingHeader clear = {};
    if (!FM25CL64B::writeObject(kHeaderAddr, clear, spi)) {
      return failWithDiag_(diag, 8, kHeaderAddr);
    }

    Result out = diag;
    out.status = Status::Passed;
    return out;
  }

  if (!FM25CL64B::writeBytes(kPatternAddr, pattern, kPatternLen, spi)) {
    return failWithDiag_(diag, 9, kPatternAddr);
  }
  diag.status_after_write = FM25CL64B::readStatusRegister(spi);
  if (!FM25CL64B::readBytes(kPatternAddr, verify, kPatternLen, spi)) {
    return failWithDiag_(diag, 10, kPatternAddr);
  }
  if (!FM25CL64B::readBytes(kPatternAddr + 1, &diag.probe_readback, 1, spi)) {
    return failWithDiag_(diag, 16, static_cast<uint16_t>(kPatternAddr + 1));
  }
  for (uint16_t i = 0; i < kPatternLen; ++i) {
    if (verify[i] != pattern[i]) {
      return failWithDiag_(diag, 11, static_cast<uint16_t>(kPatternAddr + i));
    }
  }

  header.magic = kPendingMagic;
  header.length = kPatternLen;
  header.checksum = checksum_(pattern, kPatternLen);
  header.reserved0 = 0;
  header.reserved1 = 0;
  header.reserved2 = 0;

  if (!FM25CL64B::writeObject(kHeaderAddr, header, spi)) {
    return failWithDiag_(diag, 12, kHeaderAddr);
  }

  PendingHeader check = {};
  if (!FM25CL64B::readObject(kHeaderAddr, check, spi)) {
    return failWithDiag_(diag, 13, kHeaderAddr);
  }
  if (check.magic != header.magic || check.length != header.length || check.checksum != header.checksum) {
    return failWithDiag_(diag, 14, kHeaderAddr);
  }

  Result out = diag;
  out.status = Status::NeedsPowerCycle;
  return out;
}

} // namespace FramSelfTest
