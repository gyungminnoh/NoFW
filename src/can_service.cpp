#include "app.h"
#include "can_service.h"
#include "can_transport.h"
#include "can_protocol.h"
#include "gripper_api.h"

static uint32_t g_last_rx_ms = 0;
static uint32_t g_last_tx_ms = 0;
static bool g_timeout_active = false;
static bool g_has_rx = false;
static bool g_rx_logged = false;

namespace CanService {

bool init() {
  bool ok = CanTransport::begin1Mbps();
  g_last_rx_ms = millis();
  g_last_tx_ms = g_last_rx_ms;
  g_has_rx = false;
  g_timeout_active = false;
  return ok;
}

uint32_t lastRxMs() {
  return g_last_rx_ms;
}

void poll(float current_motor_mt_rad) {
  // Receive all pending frames
  CanTransport::RxFrame f;
  while (CanTransport::poll(f)) {
    if (!g_rx_logged) {
      DebugSerial.print("[CAN] RX id=0x");
      DebugSerial.print(f.std_id, HEX);
      DebugSerial.print(" dlc=");
      DebugSerial.println(f.dlc);
    }
    if (f.std_id == CanProtocol::CAN_ID_GRIP_CMD) {
      float open_pct = 0.0f;
      if (CanProtocol::decodeGripCmd_OptionA(f.data, f.dlc, open_pct)) {
        GripperAPI::target_open_percent = open_pct;
        g_last_rx_ms = millis();
        g_has_rx = true;
        if (!g_rx_logged) {
          g_rx_logged = true;
          DebugSerial.println("[CAN] RX ok");
        }
      }
    }
  }

  // Periodic position report
  if (CAN_POS_TX_MS > 0) {
    const uint32_t now_ms = millis();
    if ((uint32_t)(now_ms - g_last_tx_ms) >= CAN_POS_TX_MS) {
      g_last_tx_ms = now_ms;
      uint8_t data[8] = {0};
      uint8_t len = 0;
      float open_pct = GripperAPI::motorMTToOutputPercentRaw(current_motor_mt_rad);
      if (CanProtocol::encodeGripPos_OptionA(open_pct, data, len)) {
        CanTransport::sendStd(CanProtocol::CAN_ID_GRIP_POS, data, len);
      }
    }
  }

  if (!g_has_rx) {
    // Keep boot target (0) until first valid CAN command arrives.
    return;
  }

  // Timeout policy: HOLD-CURRENT (target follows current output)
  // Hysteresis: require a small recovery window before clearing timeout state.
  static constexpr uint32_t RX_RECOVER_MS = 50;
  const uint32_t now_ms = millis();
  const uint32_t dt_ms = now_ms - g_last_rx_ms;

  if (dt_ms > CAN_TIMEOUT_MS) {
    if (!g_timeout_active) {
      g_timeout_active = true;
      // One-shot: capture current output once on timeout entry.
      GripperAPI::target_open_percent =
        GripperAPI::motorMTToOutputPercent(current_motor_mt_rad);
    }
  } else if (g_timeout_active && dt_ms < RX_RECOVER_MS) {
    g_timeout_active = false;
  }
}

} // namespace
