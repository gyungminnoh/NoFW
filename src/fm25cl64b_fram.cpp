#include "fm25cl64b_fram.h"

#include "board_config.h"

namespace {

constexpr uint8_t kCmdWren = 0x06;
constexpr uint8_t kCmdWrsr = 0x01;
constexpr uint8_t kCmdWrite = 0x02;
constexpr uint8_t kCmdRead = 0x03;
constexpr uint8_t kCmdRdsr = 0x05;
constexpr uint32_t kSpiHz = 1000000;

bool g_inited = false;

inline uint16_t clampAddress_(uint16_t address) {
  return address & 0x1FFFu;
}

void select_(SPIClass& spi) {
  spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_FRAM_CS, LOW);
}

void deselect_(SPIClass& spi) {
  digitalWrite(PIN_FRAM_CS, HIGH);
  spi.endTransaction();
}

void writeEnable_(SPIClass& spi) {
  select_(spi);
  spi.transfer(kCmdWren);
  deselect_(spi);
}

void sendAddress_(SPIClass& spi, uint16_t address) {
  const uint16_t a = clampAddress_(address);
  spi.transfer((uint8_t)((a >> 8) & 0xFF));
  spi.transfer((uint8_t)(a & 0xFF));
}

} // namespace

namespace FM25CL64B {

void begin(SPIClass& spi) {
  if (g_inited) return;
  pinMode(PIN_FRAM_CS, OUTPUT);
  digitalWrite(PIN_FRAM_CS, HIGH);
  spi.begin();
  g_inited = true;
}

uint8_t readStatusRegister(SPIClass& spi) {
  begin(spi);
  select_(spi);
  spi.transfer(kCmdRdsr);
  const uint8_t sr = spi.transfer(0x00);
  deselect_(spi);
  return sr;
}

void writeEnable(SPIClass& spi) {
  begin(spi);
  writeEnable_(spi);
}

void writeStatusRegister(uint8_t value, SPIClass& spi) {
  begin(spi);
  writeEnable(spi);
  select_(spi);
  spi.transfer(kCmdWrsr);
  spi.transfer(value);
  deselect_(spi);
}

bool readBytes(uint16_t address, void* dst, size_t len, SPIClass& spi) {
  if (len == 0) return true;
  if ((size_t)address + len > kCapacityBytes) return false;
  begin(spi);

  uint8_t* out = static_cast<uint8_t*>(dst);
  select_(spi);
  spi.transfer(kCmdRead);
  sendAddress_(spi, address);
  for (size_t i = 0; i < len; ++i) {
    out[i] = spi.transfer(0x00);
  }
  deselect_(spi);
  return true;
}

bool writeBytes(uint16_t address, const void* src, size_t len, SPIClass& spi) {
  if (len == 0) return true;
  if ((size_t)address + len > kCapacityBytes) return false;
  begin(spi);

  const uint8_t* in = static_cast<const uint8_t*>(src);
  writeEnable(spi);
  select_(spi);
  spi.transfer(kCmdWrite);
  sendAddress_(spi, address);
  for (size_t i = 0; i < len; ++i) {
    spi.transfer(in[i]);
  }
  deselect_(spi);
  return true;
}

} // namespace FM25CL64B
