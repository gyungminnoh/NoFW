#include "tmag5170_spi.h"

#include "board_config.h"

namespace {

constexpr uint32_t kSpiHz = 1000000;
constexpr uint32_t kDisableCrcFrame = 0x0F000407UL;
bool g_inited = false;

void select_(SPIClass& spi) {
  spi.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TMAG5170_CS, LOW);
}

void deselect_(SPIClass& spi) {
  digitalWrite(PIN_TMAG5170_CS, HIGH);
  spi.endTransaction();
}

uint32_t buildFrame_(bool is_read, uint8_t address, uint16_t data, uint8_t cmd_nibble) {
  uint32_t frame = 0;
  frame |= (static_cast<uint32_t>(is_read ? 0x80u : 0x00u) | static_cast<uint32_t>(address & 0x7Fu)) << 24;
  frame |= static_cast<uint32_t>(data) << 8;
  frame |= static_cast<uint32_t>(cmd_nibble & 0x0Fu) << 4;
  return frame;
}

uint16_t decodeStatus_(uint32_t raw) {
  const uint16_t upper = static_cast<uint16_t>((raw >> 28) & 0x0Fu);
  const uint16_t lower = static_cast<uint16_t>((raw >> 4) & 0x00FFu);
  return static_cast<uint16_t>((upper << 8) | lower);
}

} // namespace

namespace TMAG5170 {

void begin(SPIClass& spi) {
  if (g_inited) return;
  pinMode(PIN_TMAG5170_CS, OUTPUT);
  digitalWrite(PIN_TMAG5170_CS, HIGH);
  spi.begin();
  g_inited = true;
}

uint32_t transferFrame(uint32_t tx_frame, SPIClass& spi) {
  begin(spi);
  select_(spi);
  uint32_t rx_frame = 0;
  rx_frame |= static_cast<uint32_t>(spi.transfer(static_cast<uint8_t>((tx_frame >> 24) & 0xFFu))) << 24;
  rx_frame |= static_cast<uint32_t>(spi.transfer(static_cast<uint8_t>((tx_frame >> 16) & 0xFFu))) << 16;
  rx_frame |= static_cast<uint32_t>(spi.transfer(static_cast<uint8_t>((tx_frame >> 8) & 0xFFu))) << 8;
  rx_frame |= static_cast<uint32_t>(spi.transfer(static_cast<uint8_t>(tx_frame & 0xFFu)));
  deselect_(spi);
  return rx_frame;
}

uint32_t disableCrc(SPIClass& spi) {
  return transferFrame(kDisableCrcFrame, spi);
}

bool writeRegister(uint8_t address, uint16_t value, SPIClass& spi) {
  return writeRegister(address, value, 0, spi);
}

bool writeRegister(uint8_t address, uint16_t value, uint8_t cmd_nibble, SPIClass& spi) {
  transferFrame(buildFrame_(false, address, value, cmd_nibble), spi);
  return true;
}

RegisterRead readRegister(uint8_t address, SPIClass& spi) {
  return readRegister(address, 0, spi);
}

RegisterRead readRegister(uint8_t address, uint8_t cmd_nibble, SPIClass& spi) {
  RegisterRead out;
  out.raw = transferFrame(buildFrame_(true, address, 0, cmd_nibble), spi);
  out.data = static_cast<uint16_t>((out.raw >> 8) & 0xFFFFu);
  out.status = decodeStatus_(out.raw);
  return out;
}

int16_t dataToSigned(uint16_t raw_data) {
  return static_cast<int16_t>(raw_data);
}

float rawToMilliTesla(int16_t signed_code, float range_mT) {
  return (static_cast<float>(signed_code) / 32768.0f) * range_mT;
}

float rangeMilliTesla(uint16_t sensor_config, uint8_t axis_idx, DeviceVariant variant) {
  const uint8_t shift = static_cast<uint8_t>(axis_idx * 2u);
  const uint8_t range_bits = static_cast<uint8_t>((sensor_config >> shift) & 0x03u);

  if (variant == DeviceVariant::A1) {
    switch (range_bits) {
      case 0x01: return 25.0f;
      case 0x02: return 100.0f;
      default: return 50.0f;
    }
  }

  switch (range_bits) {
    case 0x01: return 75.0f;
    case 0x02: return 300.0f;
    default: return 150.0f;
  }
}

} // namespace TMAG5170
