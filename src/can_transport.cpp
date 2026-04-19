#include "app.h"
#include "can_transport.h"
#include <STM32_CAN.h>

namespace CanTransport {

static STM32_CAN g_can(CAN1, DEF);

bool begin1Mbps() {
  g_can.begin();
  g_can.setBaudRate(CAN_BITRATE);
  return true;
}

bool poll(RxFrame& out) {
  CAN_message_t msg;
  if (!g_can.read(msg)) return false;

  if (msg.flags.extended) {
    return false;
  }

  out.std_id = (uint16_t)msg.id;
  out.dlc = (uint8_t)msg.len;

  for (uint8_t i = 0; i < out.dlc && i < 8; ++i) {
    out.data[i] = msg.buf[i];
  }

  return true;
}

bool sendStd(uint16_t std_id, const uint8_t data[8], uint8_t len) {
  CAN_message_t msg;
  msg.id = std_id;
  msg.len = (len > 8) ? 8 : len;
  msg.flags.extended = 0;
  msg.flags.remote = 0;
  for (uint8_t i = 0; i < msg.len; ++i) {
    msg.buf[i] = data[i];
  }
  return g_can.write(msg);
}

} // namespace
