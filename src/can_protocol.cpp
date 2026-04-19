#include "can_protocol.h"
#include <math.h>
#include <limits.h>

namespace CanProtocol {

bool decodeGripCmd_OptionA(const uint8_t data[8], uint8_t len, float& out_open_percent) {
  if (len < 2) return false;

  int16_t cp = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
  float pct = cp * 0.01f; // centi-percent -> percent
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  out_open_percent = pct;
  return true;
}

static inline int16_t clampInt16_(int32_t x) {
  if (x > INT16_MAX) return INT16_MAX;
  if (x < INT16_MIN) return INT16_MIN;
  return (int16_t)x;
}

bool encodeGripPos_OptionA(float open_percent, uint8_t data[8], uint8_t& out_len) {
  float pct = open_percent;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  int32_t cp = (int32_t)lroundf(pct * 100.0f); // centi-percent
  int16_t cp16 = clampInt16_(cp);
  data[0] = (uint8_t)((uint16_t)cp16 & 0xFF);
  data[1] = (uint8_t)(((uint16_t)cp16 >> 8) & 0xFF);
  out_len = 2;
  return true;
}

} // namespace
