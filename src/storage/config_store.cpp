#include "storage/config_store.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "board_config.h"
#include "config/calibration_constants.h"
#include "fm25cl64b_fram.h"

namespace {

constexpr uint32_t kActuatorConfigMagic = 0x41434647UL;   // "ACFG"
constexpr uint32_t kCalibrationMagic = 0x43424C42UL;      // "CBLB"
constexpr uint16_t kActuatorConfigAddr = 0x0100;
constexpr uint16_t kCalibrationSlotAAddr = 0x0200;
constexpr uint16_t kCalibrationSlotBAddr = 0x1200;
constexpr uint16_t kStoreVersion = 2;
constexpr uint32_t kCalibrationCommitMagic = 0x434F4D54UL;  // "COMT"

struct StoredActuatorConfig {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t reserved = 0;
  ActuatorConfig config = {};
};

struct StoredCalibrationBundleSlot {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t reserved = 0;
  uint32_t sequence = 0;
  uint32_t crc32 = 0;
  uint32_t committed = 0;
  ConfigStore::CalibrationBundle bundle = {};
};

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

uint32_t calibrationBundleCrc32(uint32_t sequence, const ConfigStore::CalibrationBundle& bundle) {
  uint32_t crc = 0;
  crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&sequence), sizeof(sequence));
  crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(&bundle), sizeof(bundle));
  return crc;
}

bool isFiniteFloat(float value) {
  return isfinite(value);
}

bool sanitizeCalibrationBundle(ConfigStore::CalibrationBundle& bundle) {
  bool any_valid = false;

  const bool foc_valid =
      bundle.foc.valid && bundle.foc.magic == kCalibrationRecordMagic &&
      (bundle.foc.sensor_direction == 1 || bundle.foc.sensor_direction == -1) &&
      isFiniteFloat(bundle.foc.zero_electrical_angle);
  if (!foc_valid) {
    bundle.foc = {};
  } else {
    any_valid = true;
  }

  const bool direct_input_valid =
      bundle.direct_input.valid && bundle.direct_input.magic == kCalibrationRecordMagic &&
      isFiniteFloat(bundle.direct_input.zero_offset_rad);
  if (!direct_input_valid) {
    bundle.direct_input = {};
  } else {
    any_valid = true;
  }

  const bool as5600_valid =
      bundle.as5600.valid && bundle.as5600.magic == kCalibrationRecordMagic &&
      isFiniteFloat(bundle.as5600.zero_offset_rad);
  if (!as5600_valid) {
    bundle.as5600 = {};
  } else {
    any_valid = true;
  }

  const bool tmag_valid =
      bundle.tmag.valid && bundle.tmag.magic == kCalibrationRecordMagic &&
      isFiniteFloat(bundle.tmag.zero_offset_rad) &&
      isFiniteFloat(bundle.tmag.learned_gear_ratio) &&
      isFiniteFloat(bundle.tmag.input_phase_rad) &&
      isFiniteFloat(bundle.tmag.amp_x) &&
      isFiniteFloat(bundle.tmag.amp_y) &&
      isFiniteFloat(bundle.tmag.amp_z) &&
      isFiniteFloat(bundle.tmag.calibration_rms_rad) &&
      isFiniteFloat(bundle.tmag.validation_rms_rad) &&
      (bundle.tmag.input_sign == 1 || bundle.tmag.input_sign == -1) &&
      bundle.tmag.lut_bin_count > 0 && bundle.tmag.lut_bin_count <= kTmagLutBins &&
      bundle.tmag.valid_bin_count <= bundle.tmag.lut_bin_count;
  if (!tmag_valid) {
    bundle.tmag = {};
  } else {
    any_valid = true;
  }

  return any_valid;
}

bool readCalibrationSlot(uint16_t address, StoredCalibrationBundleSlot& out_slot) {
  if (!FM25CL64B::readObject(address, out_slot)) {
    return false;
  }
  if (out_slot.magic != kCalibrationMagic || out_slot.version != kStoreVersion) {
    return false;
  }
  if (out_slot.committed != kCalibrationCommitMagic) {
    return false;
  }
  const uint32_t expected_crc = calibrationBundleCrc32(out_slot.sequence, out_slot.bundle);
  if (out_slot.crc32 != expected_crc) {
    return false;
  }
  return sanitizeCalibrationBundle(out_slot.bundle);
}

bool loadCalibrationBundleWithStatus_(ConfigStore::CalibrationBundle& out_bundle,
                                      ConfigStore::CalibrationLoadStatus& out_status) {
  out_status = ConfigStore::CalibrationLoadStatus::None;

  StoredCalibrationBundleSlot slot_a = {};
  const bool slot_a_ok = readCalibrationSlot(kCalibrationSlotAAddr, slot_a);
  StoredCalibrationBundleSlot slot_b = {};
  const bool slot_b_ok = readCalibrationSlot(kCalibrationSlotBAddr, slot_b);

  if (slot_a_ok && slot_b_ok) {
    out_bundle = (slot_b.sequence >= slot_a.sequence) ? slot_b.bundle : slot_a.bundle;
    out_status = ConfigStore::CalibrationLoadStatus::Trusted;
    return true;
  }
  if (slot_a_ok) {
    out_bundle = slot_a.bundle;
    out_status = ConfigStore::CalibrationLoadStatus::Trusted;
    return true;
  }
  if (slot_b_ok) {
    out_bundle = slot_b.bundle;
    out_status = ConfigStore::CalibrationLoadStatus::Trusted;
    return true;
  }
  return false;
}

}  // namespace

namespace ConfigStore {

bool loadActuatorConfig(ActuatorConfig& out_config) {
  StoredActuatorConfig stored = {};
  if (!FM25CL64B::readObject(kActuatorConfigAddr, stored)) {
    return false;
  }
  if (stored.magic == kActuatorConfigMagic && stored.version == kStoreVersion) {
    out_config = stored.config;
    // CAN node identity is deployment/build-time data, not a persisted runtime
    // setting. Always take it from the currently flashed firmware.
    out_config.can_node_id = CAN_NODE_ID;
    return true;
  }
  return false;
}

bool saveActuatorConfig(const ActuatorConfig& config) {
  StoredActuatorConfig stored = {};
  stored.magic = kActuatorConfigMagic;
  stored.version = kStoreVersion;
  stored.config = config;
  // Persist the firmware-defined node ID so stored snapshots remain aligned
  // with the flashed build, but never treat FRAM as the source of truth.
  stored.config.can_node_id = CAN_NODE_ID;
  return FM25CL64B::writeObject(kActuatorConfigAddr, stored);
}

bool loadCalibrationBundle(CalibrationBundle& out_bundle) {
  CalibrationLoadStatus status = CalibrationLoadStatus::None;
  return loadCalibrationBundleWithStatus_(out_bundle, status);
}

bool loadCalibrationBundleWithStatus(CalibrationBundle& out_bundle,
                                     CalibrationLoadStatus& out_status) {
  return loadCalibrationBundleWithStatus_(out_bundle, out_status);
}

bool saveCalibrationBundle(const CalibrationBundle& bundle) {
  CalibrationBundle stored_bundle = bundle;
  if (!sanitizeCalibrationBundle(stored_bundle)) {
    return false;
  }

  StoredCalibrationBundleSlot slot_a = {};
  const bool slot_a_ok = readCalibrationSlot(kCalibrationSlotAAddr, slot_a);
  StoredCalibrationBundleSlot slot_b = {};
  const bool slot_b_ok = readCalibrationSlot(kCalibrationSlotBAddr, slot_b);

  const uint32_t next_sequence =
      (slot_a_ok || slot_b_ok)
          ? (((slot_a_ok ? slot_a.sequence : 0u) >= (slot_b_ok ? slot_b.sequence : 0u))
                 ? (slot_a_ok ? slot_a.sequence : 0u)
                 : (slot_b_ok ? slot_b.sequence : 0u)) +
                1u
          : 1u;

  const bool write_slot_a =
      !slot_a_ok || (slot_b_ok && slot_a.sequence < slot_b.sequence);
  const uint16_t target_addr = write_slot_a ? kCalibrationSlotAAddr : kCalibrationSlotBAddr;
  const uint16_t commit_addr =
      target_addr + static_cast<uint16_t>(offsetof(StoredCalibrationBundleSlot, committed));

  StoredCalibrationBundleSlot stored = {};
  stored.magic = kCalibrationMagic;
  stored.version = kStoreVersion;
  stored.sequence = next_sequence;
  stored.committed = 0;
  stored.bundle = stored_bundle;
  stored.crc32 = calibrationBundleCrc32(stored.sequence, stored.bundle);

  if (!FM25CL64B::writeObject(target_addr, stored)) {
    return false;
  }
  const uint32_t commit_magic = kCalibrationCommitMagic;
  return FM25CL64B::writeObject(commit_addr, commit_magic);
}

bool clearCalibrationBundle() {
  const StoredCalibrationBundleSlot empty_slot = {};
  return FM25CL64B::writeObject(kCalibrationSlotAAddr, empty_slot) &&
         FM25CL64B::writeObject(kCalibrationSlotBAddr, empty_slot);
}

}  // namespace ConfigStore
