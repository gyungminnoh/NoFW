#include "sensors/tmag_calibration_builder.h"

#include <limits.h>
#include <math.h>
#include <string.h>

namespace {

constexpr uint16_t kMinSampleCount = 80;
constexpr uint16_t kMinValidBinCount = 64;
constexpr float kMinAxisAmplitude = 16.0f;
constexpr int kCandidateWindowBins = 6;
constexpr float kPi = 3.14159265358979323846f;

struct AxisStats {
  int16_t min_v = INT16_MAX;
  int16_t max_v = INT16_MIN;

  void add(int16_t value) {
    if (value < min_v) min_v = value;
    if (value > max_v) max_v = value;
  }

  float amplitude() const {
    return 0.5f * (static_cast<float>(max_v) - static_cast<float>(min_v));
  }
};

float wrapPmPi(float angle_rad) {
  while (angle_rad > kPi) angle_rad -= 2.0f * kPi;
  while (angle_rad < -kPi) angle_rad += 2.0f * kPi;
  return angle_rad;
}

float wrap0To2Pi(float angle_rad) {
  while (angle_rad >= 2.0f * kPi) angle_rad -= 2.0f * kPi;
  while (angle_rad < 0.0f) angle_rad += 2.0f * kPi;
  return angle_rad;
}

int wrapBin(int idx, int bin_count) {
  while (idx < 0) idx += bin_count;
  while (idx >= bin_count) idx -= bin_count;
  return idx;
}

int circularBinDistance(int a, int b, int bin_count) {
  int distance = abs(a - b);
  if (distance > (bin_count / 2)) distance = bin_count - distance;
  return distance;
}

int angleToBin(float angle_rad, int bin_count) {
  const float wrapped = wrap0To2Pi(angle_rad);
  int idx = static_cast<int>(floorf((wrapped * static_cast<float>(bin_count)) / (2.0f * kPi)));
  if (idx < 0) idx = 0;
  if (idx >= bin_count) idx = bin_count - 1;
  return idx;
}

float binToAngle(int bin, int bin_count) {
  return ((static_cast<float>(bin) + 0.5f) * (2.0f * kPi)) / static_cast<float>(bin_count);
}

int outputCandidateCount(float gear_ratio) {
  const int rounded = static_cast<int>(lroundf(fabsf(gear_ratio)));
  if (rounded < 1) return 1;
  return rounded;
}

float scoreLutBin(const TmagCalibrationData& calibration,
                  int16_t x_raw,
                  int16_t y_raw,
                  int16_t z_raw,
                  int bin) {
  const float dx = (static_cast<float>(x_raw) - static_cast<float>(calibration.lut[bin].x)) / calibration.amp_x;
  const float dy = (static_cast<float>(y_raw) - static_cast<float>(calibration.lut[bin].y)) / calibration.amp_y;
  const float dz = (static_cast<float>(z_raw) - static_cast<float>(calibration.lut[bin].z)) / calibration.amp_z;
  return dx * dx + dy * dy + dz * dz;
}

int candidateBestBin(const TmagCalibrationData& calibration,
                     int16_t x_raw,
                     int16_t y_raw,
                     int16_t z_raw,
                     float input_single_turn_rad) {
  const int bin_count = static_cast<int>(calibration.lut_bin_count);
  const int candidate_count = outputCandidateCount(calibration.learned_gear_ratio);

  int best_bin = 0;
  float best_score = 1e30f;
  bool visited[kTmagLutBins] = {};

  for (int candidate = 0; candidate < candidate_count; ++candidate) {
    const float candidate_angle = wrap0To2Pi(
        (static_cast<float>(calibration.input_sign) *
         (input_single_turn_rad - calibration.input_phase_rad + (2.0f * kPi * candidate))) /
        calibration.learned_gear_ratio);
    const int center_bin = angleToBin(candidate_angle, bin_count);

    for (int delta = -kCandidateWindowBins; delta <= kCandidateWindowBins; ++delta) {
      const int bin = wrapBin(center_bin + delta, bin_count);
      if (visited[bin]) continue;
      visited[bin] = true;

      const float score = scoreLutBin(calibration, x_raw, y_raw, z_raw, bin);
      if (score < best_score) {
        best_score = score;
        best_bin = bin;
      }
    }
  }

  return best_bin;
}

float estimateInputPhase(const TmagCalibrationSample* samples,
                         size_t sample_count,
                         float learned_gear_ratio,
                         int sign) {
  float phase_sin = 0.0f;
  float phase_cos = 0.0f;

  for (size_t i = 0; i < sample_count; ++i) {
    const float ref_wrapped = wrap0To2Pi(samples[i].reference_angle_rad);
    const float phase = wrap0To2Pi(
        samples[i].input_angle_rad - (static_cast<float>(sign) * learned_gear_ratio * ref_wrapped));
    phase_sin += sinf(phase);
    phase_cos += cosf(phase);
  }

  float phase = atan2f(phase_sin, phase_cos);
  if (phase < 0.0f) phase += 2.0f * kPi;
  return phase;
}

void fillInterpolatedLut(const int32_t* sum_x,
                         const int32_t* sum_y,
                         const int32_t* sum_z,
                         const uint16_t* counts,
                         uint16_t bin_count,
                         TmagCalibrationData& calibration,
                         uint16_t& valid_bin_count) {
  valid_bin_count = 0;
  for (uint16_t i = 0; i < bin_count; ++i) {
    if (counts[i] == 0) continue;
    calibration.lut[i].x =
        static_cast<int16_t>(lroundf(static_cast<float>(sum_x[i]) / static_cast<float>(counts[i])));
    calibration.lut[i].y =
        static_cast<int16_t>(lroundf(static_cast<float>(sum_y[i]) / static_cast<float>(counts[i])));
    calibration.lut[i].z =
        static_cast<int16_t>(lroundf(static_cast<float>(sum_z[i]) / static_cast<float>(counts[i])));
    ++valid_bin_count;
  }

  for (uint16_t i = 0; i < bin_count; ++i) {
    if (counts[i] > 0) continue;

    int prev = -1;
    int next = -1;
    for (uint16_t step = 1; step < bin_count; ++step) {
      const int idx = wrapBin(static_cast<int>(i) - static_cast<int>(step), bin_count);
      if (counts[idx] > 0) {
        prev = idx;
        break;
      }
    }
    for (uint16_t step = 1; step < bin_count; ++step) {
      const int idx = wrapBin(static_cast<int>(i) + static_cast<int>(step), bin_count);
      if (counts[idx] > 0) {
        next = idx;
        break;
      }
    }

    if (prev < 0 && next < 0) {
      continue;
    }
    if (prev < 0) prev = next;
    if (next < 0) next = prev;

    const int dist_prev = circularBinDistance(static_cast<int>(i), prev, bin_count);
    const int dist_next = circularBinDistance(static_cast<int>(i), next, bin_count);
    const int span = dist_prev + dist_next;

    if (span <= 0) {
      calibration.lut[i] = calibration.lut[prev];
      continue;
    }

    const float t = static_cast<float>(dist_prev) / static_cast<float>(span);
    calibration.lut[i].x = static_cast<int16_t>(lroundf(
        static_cast<float>(calibration.lut[prev].x) * (1.0f - t) +
        static_cast<float>(calibration.lut[next].x) * t));
    calibration.lut[i].y = static_cast<int16_t>(lroundf(
        static_cast<float>(calibration.lut[prev].y) * (1.0f - t) +
        static_cast<float>(calibration.lut[next].y) * t));
    calibration.lut[i].z = static_cast<int16_t>(lroundf(
        static_cast<float>(calibration.lut[prev].z) * (1.0f - t) +
        static_cast<float>(calibration.lut[next].z) * t));
  }
}

}  // namespace

void TmagCalibrationBuilder::reset(float learned_gear_ratio) {
  sample_count_ = 0;
  learned_gear_ratio_ = learned_gear_ratio;
}

bool TmagCalibrationBuilder::addSample(const TmagCalibrationSample& sample) {
  if (sample_count_ >= kMaxSamples) {
    return false;
  }
  samples_[sample_count_++] = sample;
  return true;
}

size_t TmagCalibrationBuilder::sampleCount() const {
  return sample_count_;
}

bool TmagCalibrationBuilder::build(TmagCalibrationData& out_calibration,
                                   TmagCalibrationMetrics* out_metrics) const {
  if (sample_count_ < kMinSampleCount || fabsf(learned_gear_ratio_) < 1.0f) {
    return false;
  }

  AxisStats axis_x = {};
  AxisStats axis_y = {};
  AxisStats axis_z = {};
  for (size_t i = 0; i < sample_count_; ++i) {
    axis_x.add(samples_[i].x);
    axis_y.add(samples_[i].y);
    axis_z.add(samples_[i].z);
  }

  TmagCalibrationData calibration = {};
  calibration.learned_gear_ratio = learned_gear_ratio_;
  calibration.amp_x = axis_x.amplitude();
  calibration.amp_y = axis_y.amplitude();
  calibration.amp_z = axis_z.amplitude();
  calibration.lut_bin_count = kTmagLutBins;
  calibration.zero_offset_rad = 0.0f;
  calibration.valid = true;

  if (calibration.amp_x < kMinAxisAmplitude ||
      calibration.amp_y < kMinAxisAmplitude ||
      calibration.amp_z < kMinAxisAmplitude) {
    return false;
  }

  int32_t sum_x[kTmagLutBins] = {};
  int32_t sum_y[kTmagLutBins] = {};
  int32_t sum_z[kTmagLutBins] = {};
  uint16_t counts[kTmagLutBins] = {};

  for (size_t i = 0; i < sample_count_; ++i) {
    const int bin = angleToBin(samples_[i].reference_angle_rad, kTmagLutBins);
    sum_x[bin] += samples_[i].x;
    sum_y[bin] += samples_[i].y;
    sum_z[bin] += samples_[i].z;
    ++counts[bin];
  }

  fillInterpolatedLut(sum_x, sum_y, sum_z, counts, kTmagLutBins, calibration, calibration.valid_bin_count);
  if (calibration.valid_bin_count < kMinValidBinCount) {
    return false;
  }

  float best_err2 = 1e30f;
  int best_sign = 1;
  float best_phase = 0.0f;
  const int signs[2] = {1, -1};
  for (int sign : signs) {
    calibration.input_sign = static_cast<int8_t>(sign);
    calibration.input_phase_rad = estimateInputPhase(samples_, sample_count_, learned_gear_ratio_, sign);

    float err2 = 0.0f;
    for (size_t i = 0; i < sample_count_; ++i) {
      float estimated_angle = 0.0f;
      if (!estimateAngleRad(calibration,
                            samples_[i].x,
                            samples_[i].y,
                            samples_[i].z,
                            samples_[i].input_angle_rad,
                            estimated_angle)) {
        return false;
      }
      const float ref = wrap0To2Pi(samples_[i].reference_angle_rad);
      const float err = wrapPmPi(estimated_angle - ref);
      err2 += err * err;
    }

    if (err2 < best_err2) {
      best_err2 = err2;
      best_sign = sign;
      best_phase = calibration.input_phase_rad;
    }
  }

  calibration.input_sign = static_cast<int8_t>(best_sign);
  calibration.input_phase_rad = best_phase;
  calibration.calibration_rms_rad = sqrtf(best_err2 / static_cast<float>(sample_count_));
  calibration.validation_rms_rad = 0.0f;
  out_calibration = calibration;
  if (out_metrics != nullptr) {
    *out_metrics = {};
    out_metrics->sample_count = sample_count_;
    out_metrics->valid_bin_count = calibration.valid_bin_count;
    out_metrics->calibration_rms_rad = calibration.calibration_rms_rad;
  }
  return true;
}

bool TmagCalibrationBuilder::estimateAngleRad(const TmagCalibrationData& calibration,
                                              int16_t x_raw,
                                              int16_t y_raw,
                                              int16_t z_raw,
                                              float input_single_turn_rad,
                                              float& out_angle_rad,
                                              int* out_best_bin) {
  if (!calibration.valid || calibration.lut_bin_count == 0 ||
      calibration.lut_bin_count > kTmagLutBins ||
      calibration.amp_x <= 0.0f || calibration.amp_y <= 0.0f || calibration.amp_z <= 0.0f ||
      fabsf(calibration.learned_gear_ratio) < 1.0f) {
    return false;
  }

  const int best_bin = candidateBestBin(calibration, x_raw, y_raw, z_raw, input_single_turn_rad);
  if (out_best_bin != nullptr) {
    *out_best_bin = best_bin;
  }

  out_angle_rad = wrap0To2Pi(binToAngle(best_bin, calibration.lut_bin_count) - calibration.zero_offset_rad);
  return true;
}
