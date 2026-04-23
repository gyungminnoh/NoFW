#include "app.h"
#include <Wire.h>
#include <math.h>

static bool wire_inited_ = false;

static void ensureWireInit_() {
  if (wire_inited_) return;
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();
  Wire.setClock(100000);
  wire_inited_ = true;
}

static bool as5600ReadRawAngle_(uint16_t& out_raw) {
  // AS5600 RAW_ANGLE register: 0x0C (high), 0x0D (low)
  Wire.beginTransmission(AS5600_I2C_ADDR);
  Wire.write(0x0C);
  uint8_t tx_err = Wire.endTransmission(false);
  if (tx_err != 0) {
    return false;
  }

  int rx = Wire.requestFrom((int)AS5600_I2C_ADDR, 2);
  if (rx != 2) {
    return false;
  }

  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  uint16_t raw = ((uint16_t)hi << 8) | lo;
  raw &= 0x0FFF; // 12-bit
  out_raw = raw;
  return true;
}

static float wrapToPi_(float a) {
  while (a > PI) a -= (2.0f * PI);
  while (a < -PI) a += (2.0f * PI);
  return a;
}

static bool readAs5600AngleRadSamples_(int samples, int delay_ms, float& out_rad) {
  if (samples <= 0) return false;

  ensureWireInit_();

  float sum_sin = 0.0f;
  float sum_cos = 0.0f;
  float angles[8] = {};
  if (samples > 8) samples = 8;

  for (int i = 0; i < samples; ++i) {
    uint16_t raw = 0;
    if (!as5600ReadRawAngle_(raw)) {
      return false;
    }
    float rad = (raw * _2PI) / 4096.0f;
    angles[i] = rad;
    sum_sin += sinf(rad);
    sum_cos += cosf(rad);
    if (delay_ms > 0) delay(delay_ms);
  }

  float mean = atan2f(sum_sin, sum_cos);
  if (mean < 0.0f) mean += _2PI;

  const float kMaxSpanRad = 0.08f; // ~4.6 deg
  float max_err = 0.0f;
  for (int i = 0; i < samples; ++i) {
    float d = wrapToPi_(mean - angles[i]);
    float ad = fabsf(d);
    if (ad > max_err) max_err = ad;
  }
  if (max_err > kMaxSpanRad) {
    return false;
  }

  out_rad = mean;
  return true;
}

bool readAs5600AngleRad(float& out_rad) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (readAs5600AngleRadSamples_(5, 2, out_rad)) {
      return true;
    }
    delay(5);
  }
  return false;
}
