#include "storage/config_store.h"

#include <string.h>

#include "config/calibration_constants.h"
#include "fm25cl64b_fram.h"

namespace {

constexpr uint32_t kActuatorConfigMagic = 0x41434647UL;   // "ACFG"
constexpr uint32_t kCalibrationMagic = 0x43424C42UL;      // "CBLB"
constexpr uint16_t kLegacyCalibrationAddr = 0x0000;
constexpr uint16_t kActuatorConfigAddr = 0x0100;
constexpr uint16_t kCalibrationAddr = 0x0200;
constexpr uint16_t kStoreVersion = 2;
constexpr float kDegPerRad = 180.0f / 3.1415926f;

struct ActuatorConfigV1 {
  uint32_t version = 1;
  OutputEncoderType output_encoder_type = OutputEncoderType::As5600;
  ControlMode default_control_mode = ControlMode::OutputAngle;
  float gear_ratio = 1.0f;
  int8_t motor_to_output_sign = 1;
  float output_min_rad = 0.0f;
  float output_max_rad = 0.0f;
  bool enable_velocity_mode = true;
  bool enable_output_angle_mode = true;
  uint8_t can_node_id = 7;
};

struct CalibrationBundleV1 {
  FocCalibrationData foc = {};
  As5600CalibrationData as5600 = {};
  TmagCalibrationData tmag = {};
};

struct StoredActuatorConfig {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t reserved = 0;
  ActuatorConfig config = {};
};

struct StoredCalibrationBundle {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t reserved = 0;
  ConfigStore::CalibrationBundle bundle = {};
};

struct StoredActuatorConfigV1 {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t reserved = 0;
  ActuatorConfigV1 config = {};
};

struct StoredCalibrationBundleV1 {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t reserved = 0;
  CalibrationBundleV1 bundle = {};
};

struct LegacyCalibrationRecord {
  uint32_t magic = 0;
  int8_t sensor_dir = 1;
  float zero_elec = 0.0f;
  float as5600_zero_ref = 0.0f;
};

bool loadLegacyCalibrationRecord(LegacyCalibrationRecord& out_record) {
  if (!FM25CL64B::readObject(kLegacyCalibrationAddr, out_record)) {
    return false;
  }
  if (out_record.magic != kCalibrationRecordMagic) {
    return false;
  }
  if (out_record.sensor_dir != 1 && out_record.sensor_dir != -1) {
    return false;
  }
  return true;
}

void saveLegacyCalibrationRecord(const ConfigStore::CalibrationBundle& bundle) {
  if (!bundle.foc.valid || !bundle.as5600.valid) {
    return;
  }

  LegacyCalibrationRecord legacy = {};
  legacy.magic = kCalibrationRecordMagic;
  legacy.sensor_dir = bundle.foc.sensor_direction;
  legacy.zero_elec = bundle.foc.zero_electrical_angle;
  legacy.as5600_zero_ref = bundle.as5600.zero_offset_rad;
  FM25CL64B::writeObject(kLegacyCalibrationAddr, legacy);
}

void clearLegacyCalibrationRecord() {
  const uint32_t empty_magic = 0;
  FM25CL64B::writeObject(kLegacyCalibrationAddr, empty_magic);
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
    return true;
  }

  StoredActuatorConfigV1 legacy = {};
  if (!FM25CL64B::readObject(kActuatorConfigAddr, legacy)) {
    return false;
  }
  if (legacy.magic != kActuatorConfigMagic || legacy.version != 1) {
    return false;
  }

  out_config = {};
  out_config.version = 2;
  out_config.output_encoder_type = legacy.config.output_encoder_type;
  out_config.default_control_mode = legacy.config.default_control_mode;
  out_config.gear_ratio = legacy.config.gear_ratio;
  out_config.motor_to_output_sign = legacy.config.motor_to_output_sign;
  out_config.output_min_deg = legacy.config.output_min_rad * kDegPerRad;
  out_config.output_max_deg = legacy.config.output_max_rad * kDegPerRad;
  out_config.enable_velocity_mode = legacy.config.enable_velocity_mode;
  out_config.enable_output_angle_mode = legacy.config.enable_output_angle_mode;
  out_config.can_node_id = legacy.config.can_node_id;
  return true;
}

bool saveActuatorConfig(const ActuatorConfig& config) {
  StoredActuatorConfig stored = {};
  stored.magic = kActuatorConfigMagic;
  stored.version = kStoreVersion;
  stored.config = config;
  return FM25CL64B::writeObject(kActuatorConfigAddr, stored);
}

bool loadCalibrationBundle(CalibrationBundle& out_bundle) {
  StoredCalibrationBundle stored = {};
  if (!FM25CL64B::readObject(kCalibrationAddr, stored)) {
    return false;
  }
  if (stored.magic == kCalibrationMagic && stored.version == kStoreVersion) {
    out_bundle = stored.bundle;
    return true;
  }

  StoredCalibrationBundleV1 legacy = {};
  if (!FM25CL64B::readObject(kCalibrationAddr, legacy)) {
    return false;
  }
  if (legacy.magic != kCalibrationMagic || legacy.version != 1) {
    return false;
  }

  out_bundle = {};
  out_bundle.foc = legacy.bundle.foc;
  out_bundle.as5600 = legacy.bundle.as5600;
  out_bundle.tmag = legacy.bundle.tmag;
  return true;
}

bool saveCalibrationBundle(const CalibrationBundle& bundle) {
  StoredCalibrationBundle stored = {};
  stored.magic = kCalibrationMagic;
  stored.version = kStoreVersion;
  stored.bundle = bundle;
  return FM25CL64B::writeObject(kCalibrationAddr, stored);
}

bool clearCalibrationBundle() {
  StoredCalibrationBundle stored = {};
  return FM25CL64B::writeObject(kCalibrationAddr, stored);
}

bool loadCalibrationBundleCompat(CalibrationBundle& out_bundle) {
  if (loadCalibrationBundle(out_bundle)) {
    return out_bundle.foc.valid || out_bundle.direct_input.valid ||
           out_bundle.as5600.valid || out_bundle.tmag.valid;
  }

  LegacyCalibrationRecord legacy = {};
  if (!loadLegacyCalibrationRecord(legacy)) {
    return false;
  }

  out_bundle = {};
  out_bundle.foc.magic = legacy.magic;
  out_bundle.foc.sensor_direction = legacy.sensor_dir;
  out_bundle.foc.zero_electrical_angle = legacy.zero_elec;
  out_bundle.foc.valid = true;

  out_bundle.as5600.magic = legacy.magic;
  out_bundle.as5600.zero_offset_rad = legacy.as5600_zero_ref;
  out_bundle.as5600.invert = false;
  out_bundle.as5600.valid = true;
  return true;
}

bool saveCalibrationBundleCompat(const CalibrationBundle& bundle) {
  if (!saveCalibrationBundle(bundle)) {
    return false;
  }
  saveLegacyCalibrationRecord(bundle);
  return true;
}

bool clearCalibrationBundleCompat() {
  clearLegacyCalibrationRecord();
  return clearCalibrationBundle();
}

}  // namespace ConfigStore
