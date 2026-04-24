#include "app.h"
#include "actuator_api.h"
#include "can_service.h"
#include "can_protocol.h"
#include "can_transport.h"

static uint32_t g_last_rx_ms = 0;
static uint32_t g_last_tx_ms = 0;
static bool g_timeout_active = false;
static bool g_has_rx = false;
static uint8_t g_can_node_id = CAN_NODE_ID;
static ControlMode g_default_control_mode = ControlMode::OutputAngle;
static bool g_enable_output_angle_mode = true;
static bool g_enable_velocity_mode = true;
static bool g_has_pending_profile_change = false;
static OutputEncoderType g_pending_profile_change = OutputEncoderType::As5600;
static bool g_has_pending_power_stage_enable = false;
static bool g_pending_power_stage_enable = false;
static bool g_has_pending_actuator_limits_config = false;
static float g_pending_output_min_deg = 0.0f;
static float g_pending_output_max_deg = 0.0f;
static bool g_has_pending_actuator_gear_config = false;
static float g_pending_gear_ratio = 1.0f;
static bool g_has_pending_output_encoder_config = false;
static OutputEncoderType g_pending_output_encoder_type = OutputEncoderType::As5600;
static bool g_pending_output_encoder_invert = false;
static bool g_has_pending_output_encoder_auto_cal = false;
static OutputEncoderType g_pending_output_encoder_auto_cal_type = OutputEncoderType::As5600;
static float g_last_pos_mt_rad = 0.0f;
static uint32_t g_last_pos_sample_ms = 0;

namespace CanService {

void configure(const ActuatorConfig& actuator_config) {
  g_last_rx_ms = millis();
  g_has_rx = false;
  g_timeout_active = false;
  g_can_node_id = actuator_config.can_node_id;
  g_default_control_mode = actuator_config.default_control_mode;
  g_enable_output_angle_mode = actuator_config.enable_output_angle_mode;
  g_enable_velocity_mode = actuator_config.enable_velocity_mode;
}

bool init(const ActuatorConfig& actuator_config) {
  bool ok = CanTransport::begin1Mbps();
  configure(actuator_config);
  g_last_tx_ms = g_last_rx_ms;
  g_has_pending_profile_change = false;
  g_has_pending_power_stage_enable = false;
  g_has_pending_actuator_limits_config = false;
  g_has_pending_actuator_gear_config = false;
  g_has_pending_output_encoder_config = false;
  g_has_pending_output_encoder_auto_cal = false;
  g_last_pos_mt_rad = 0.0f;
  g_last_pos_sample_ms = 0;
  return ok;
}

void poll(float current_motor_mt_rad) {
  const uint32_t now_ms = millis();
  const float output_angle_deg = ActuatorAPI::motorMTToOutputRawDeg(current_motor_mt_rad);

  // Receive all pending frames
  CanTransport::RxFrame f;
  while (CanTransport::poll(f)) {
    if (f.std_id == CanProtocol::outputAngleCmdCanId(g_can_node_id)) {
      float target_angle_deg = 0.0f;
      if (!g_enable_output_angle_mode ||
          !CanProtocol::decodeOutputAngleDeg_OptionA(f.data, f.dlc, target_angle_deg)) {
        continue;
      }
      ActuatorAPI::target_output_deg = target_angle_deg;
      ActuatorAPI::active_control_mode = ControlMode::OutputAngle;
      g_last_rx_ms = millis();
      g_has_rx = true;
      continue;
    }

    if (f.std_id == CanProtocol::outputVelocityCmdCanId(g_can_node_id)) {
      float target_velocity_deg_s = 0.0f;
      if (!g_enable_velocity_mode ||
          !CanProtocol::decodeOutputVelocityDegPerSec_OptionA(
              f.data, f.dlc, target_velocity_deg_s)) {
        continue;
      }
      ActuatorAPI::target_output_velocity_deg_s = target_velocity_deg_s;
      ActuatorAPI::active_control_mode = ControlMode::OutputVelocity;
      g_last_rx_ms = millis();
      g_has_rx = true;
      continue;
    }

    if (f.std_id == CanProtocol::outputProfileCmdCanId(g_can_node_id)) {
      OutputEncoderType requested_profile = OutputEncoderType::As5600;
      if (!CanProtocol::decodeOutputProfileCmd_OptionA(f.data, f.dlc, requested_profile)) {
        continue;
      }
      g_pending_profile_change = requested_profile;
      g_has_pending_profile_change = true;
      continue;
    }

    if (f.std_id == CanProtocol::powerStageCmdCanId(g_can_node_id)) {
      bool enable = false;
      if (!CanProtocol::decodePowerStageCmd_OptionA(f.data, f.dlc, enable)) {
        continue;
      }
      g_pending_power_stage_enable = enable;
      g_has_pending_power_stage_enable = true;
      continue;
    }

    if (f.std_id == CanProtocol::actuatorLimitsConfigCmdCanId(g_can_node_id)) {
      float output_min_deg = 0.0f;
      float output_max_deg = 0.0f;
      if (!CanProtocol::decodeActuatorLimitsConfigCmd_OptionA(
              f.data, f.dlc, output_min_deg, output_max_deg)) {
        continue;
      }
      g_pending_output_min_deg = output_min_deg;
      g_pending_output_max_deg = output_max_deg;
      g_has_pending_actuator_limits_config = true;
      continue;
    }

    if (f.std_id == CanProtocol::actuatorGearConfigCmdCanId(g_can_node_id)) {
      float gear_ratio = 1.0f;
      if (!CanProtocol::decodeActuatorGearConfigCmd_OptionA(f.data, f.dlc, gear_ratio)) {
        continue;
      }
      g_pending_gear_ratio = gear_ratio;
      g_has_pending_actuator_gear_config = true;
      continue;
    }

    if (f.std_id == CanProtocol::outputEncoderConfigCmdCanId(g_can_node_id)) {
      OutputEncoderType encoder_type = OutputEncoderType::As5600;
      bool invert = false;
      if (!CanProtocol::decodeOutputEncoderConfigCmd_OptionA(
              f.data, f.dlc, encoder_type, invert)) {
        continue;
      }
      g_pending_output_encoder_type = encoder_type;
      g_pending_output_encoder_invert = invert;
      g_has_pending_output_encoder_config = true;
      continue;
    }

    if (f.std_id == CanProtocol::outputEncoderAutoCalCmdCanId(g_can_node_id)) {
      OutputEncoderType encoder_type = OutputEncoderType::As5600;
      if (!CanProtocol::decodeOutputEncoderAutoCalCmd_OptionA(
              f.data, f.dlc, encoder_type)) {
        continue;
      }
      g_pending_output_encoder_auto_cal_type = encoder_type;
      g_has_pending_output_encoder_auto_cal = true;
    }
  }

  // Periodic angle / velocity report
  if (CAN_STATUS_TX_MS > 0) {
    if ((uint32_t)(now_ms - g_last_tx_ms) >= CAN_STATUS_TX_MS) {
      g_last_tx_ms = now_ms;
      float output_velocity_deg_s = 0.0f;
      if (g_last_pos_sample_ms != 0 && now_ms > g_last_pos_sample_ms) {
        const float dt_s = static_cast<float>(now_ms - g_last_pos_sample_ms) * 0.001f;
        const float motor_velocity = (current_motor_mt_rad - g_last_pos_mt_rad) / dt_s;
        output_velocity_deg_s =
            ActuatorAPI::motorVelocityToOutputVelocityDegPerSec(motor_velocity);
      }
      g_last_pos_mt_rad = current_motor_mt_rad;
      g_last_pos_sample_ms = now_ms;

      uint8_t angle_data[8] = {0};
      uint8_t angle_len = 0;
      if (CanProtocol::encodeOutputAngleDeg_OptionA(output_angle_deg, angle_data, angle_len)) {
        CanTransport::sendStd(
            CanProtocol::outputAngleStatusCanId(g_can_node_id), angle_data, angle_len);
      }

      uint8_t velocity_data[8] = {0};
      uint8_t velocity_len = 0;
      if (CanProtocol::encodeOutputVelocityDegPerSec_OptionA(
              output_velocity_deg_s, velocity_data, velocity_len)) {
        CanTransport::sendStd(
            CanProtocol::outputVelocityStatusCanId(g_can_node_id), velocity_data, velocity_len);
      }
    }
  }

  if (!g_has_rx) {
    ActuatorAPI::active_control_mode = g_default_control_mode;
    return;
  }

  // Timeout policy: HOLD-CURRENT (target follows current output)
  // Hysteresis: require a small recovery window before clearing timeout state.
  static constexpr uint32_t RX_RECOVER_MS = 50;
  const uint32_t dt_ms = now_ms - g_last_rx_ms;

  if (dt_ms > CAN_TIMEOUT_MS) {
    if (!g_timeout_active) {
      g_timeout_active = true;
      if (ActuatorAPI::active_control_mode == ControlMode::OutputVelocity) {
        ActuatorAPI::target_output_velocity_deg_s = 0.0f;
      } else if (g_enable_output_angle_mode) {
        ActuatorAPI::target_output_deg = output_angle_deg;
      }
    }
  } else if (g_timeout_active && dt_ms < RX_RECOVER_MS) {
    g_timeout_active = false;
  }
}

bool takePendingOutputProfileChange(OutputEncoderType& out_profile) {
  if (!g_has_pending_profile_change) {
    return false;
  }
  out_profile = g_pending_profile_change;
  g_has_pending_profile_change = false;
  return true;
}

bool takePendingPowerStageEnable(bool& out_enable) {
  if (!g_has_pending_power_stage_enable) {
    return false;
  }
  out_enable = g_pending_power_stage_enable;
  g_has_pending_power_stage_enable = false;
  return true;
}

bool takePendingActuatorLimitsConfig(float& out_output_min_deg,
                                     float& out_output_max_deg) {
  if (!g_has_pending_actuator_limits_config) {
    return false;
  }
  out_output_min_deg = g_pending_output_min_deg;
  out_output_max_deg = g_pending_output_max_deg;
  g_has_pending_actuator_limits_config = false;
  return true;
}

bool takePendingActuatorGearConfig(float& out_gear_ratio) {
  if (!g_has_pending_actuator_gear_config) {
    return false;
  }
  out_gear_ratio = g_pending_gear_ratio;
  g_has_pending_actuator_gear_config = false;
  return true;
}

bool takePendingOutputEncoderConfig(OutputEncoderType& out_encoder_type,
                                    bool& out_invert) {
  if (!g_has_pending_output_encoder_config) {
    return false;
  }
  out_encoder_type = g_pending_output_encoder_type;
  out_invert = g_pending_output_encoder_invert;
  g_has_pending_output_encoder_config = false;
  return true;
}

bool takePendingOutputEncoderAutoCalibration(OutputEncoderType& out_encoder_type) {
  if (!g_has_pending_output_encoder_auto_cal) {
    return false;
  }
  out_encoder_type = g_pending_output_encoder_auto_cal_type;
  g_has_pending_output_encoder_auto_cal = false;
  return true;
}

} // namespace
