#include <Arduino.h>
#include <STM32_CAN.h>

STM32_CAN Can1(CAN1, DEF);
static constexpr uint8_t kNodeCount = 1;
static constexpr uint8_t kNodeIds[kNodeCount] = {7};
static constexpr uint16_t kPosBaseId = 0x400;
static constexpr uint16_t kCmdBaseId = 0x200;
static constexpr uint32_t kCmdRepeatMs = 50;

static bool parseOpenPercent(const String& line, float& out_pct, uint8_t& out_node_id, bool& out_all) {
  if (line.length() == 0) return false;
  out_all = true;
  out_node_id = 0;

  int sep = line.indexOf(':');
  if (sep > 0) {
    String lhs = line.substring(0, sep);
    String rhs = line.substring(sep + 1);
    lhs.trim();
    rhs.trim();
    out_node_id = (uint8_t)lhs.toInt();
    out_all = false;
    out_pct = rhs.toFloat();
    return true;
  }

  if (line.startsWith("all")) {
    String rhs = line.substring(3);
    rhs.trim();
    out_pct = rhs.toFloat();
    return true;
  }

  out_pct = line.toFloat();
  return true;
}

static bool parseHexU16_(const String& token, uint16_t& out) {
  if (token.length() == 0) return false;
  char* endp = nullptr;
  unsigned long v = strtoul(token.c_str(), &endp, 16);
  if (endp == token.c_str() || *endp != '\0' || v > 0xFFFFUL) return false;
  out = (uint16_t)v;
  return true;
}

static bool parseHexByte_(const String& token, uint8_t& out) {
  uint16_t v = 0;
  if (!parseHexU16_(token, v)) return false;
  if (v > 0xFFU) return false;
  out = (uint8_t)v;
  return true;
}

static bool parseCanSendLine_(const String& line, uint16_t& out_id, uint8_t out_data[8], uint8_t& out_len) {
  if (!line.startsWith("send")) return false;
  String rest = line.substring(4);
  rest.trim();
  if (rest.length() == 0) return false;

  int hash = rest.indexOf('#');
  if (hash >= 0) {
    String id_str = rest.substring(0, hash);
    String data_str = rest.substring(hash + 1);
    id_str.trim();
    data_str.trim();
    if (id_str.length() == 0) return false;
    uint16_t idv = 0;
    if (!parseHexU16_(id_str, idv) || idv > 0x7FFU) return false;
    out_id = idv;

    if (data_str.length() % 2 != 0) return false;
    uint8_t len = (uint8_t)(data_str.length() / 2);
    if (len > 8) return false;
    for (uint8_t i = 0; i < len; ++i) {
      String byte_str = data_str.substring(i * 2, i * 2 + 2);
      uint8_t b = 0;
      if (!parseHexByte_(byte_str, b)) return false;
      out_data[i] = b;
    }
    out_len = len;
    return true;
  }

  // Space-separated: send <id> <b0> <b1> ...
  int first_sp = rest.indexOf(' ');
  String id_str = (first_sp < 0) ? rest : rest.substring(0, first_sp);
  id_str.trim();
  if (id_str.length() == 0) return false;
  uint16_t idv = 0;
  if (!parseHexU16_(id_str, idv) || idv > 0x7FFU) return false;
  out_id = idv;

  out_len = 0;
  String bytes_str = (first_sp < 0) ? String("") : rest.substring(first_sp + 1);
  bytes_str.trim();
  while (bytes_str.length() > 0 && out_len < 8) {
    int sp = bytes_str.indexOf(' ');
    String tok = (sp < 0) ? bytes_str : bytes_str.substring(0, sp);
    tok.trim();
    if (tok.length() > 0) {
      uint8_t b = 0;
      if (!parseHexByte_(tok, b)) return false;
      out_data[out_len++] = b;
    }
    if (sp < 0) break;
    bytes_str = bytes_str.substring(sp + 1);
    bytes_str.trim();
  }
  return true;
}

static bool findNodeFromId(uint16_t std_id, uint8_t& out_node_id) {
  for (uint8_t i = 0; i < kNodeCount; ++i) {
    if (std_id == (uint16_t)(kPosBaseId + kNodeIds[i])) {
      out_node_id = kNodeIds[i];
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Can1.begin();
  Can1.setBaudRate(1000000);
  Serial.println("[CAN] Input: pct | all pct | node:pct (e.g., 7:25.0)");
  Serial.println("[CAN] RX print toggle: m (mute/unmute) or rx on/off");
  Serial.println("[CAN] Dump toggle: dump on/off");
  Serial.println("[CAN] Raw send: send <id>#<hexdata> or send <id> <b0> <b1> ...");
}

void loop() {
  static bool rx_print = true;
  static bool dump_all = false;
  static bool have_cmd = false;
  static float last_cmd_pct = 0.0f;
  static bool last_send_all = true;
  static uint8_t last_target_node = 0;
  static uint32_t last_tx_ms = 0;

  // TX: send angle command from serial input
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.equalsIgnoreCase("m") || line.equalsIgnoreCase("mute")) {
      rx_print = !rx_print;
      Serial.print("[CAN] RX print ");
      Serial.println(rx_print ? "ON" : "OFF");
    } else if (line.equalsIgnoreCase("rx on")) {
      rx_print = true;
      Serial.println("[CAN] RX print ON");
    } else if (line.equalsIgnoreCase("rx off")) {
      rx_print = false;
      Serial.println("[CAN] RX print OFF");
    } else if (line.equalsIgnoreCase("dump on")) {
      dump_all = true;
      Serial.println("[CAN] Dump ON");
    } else if (line.equalsIgnoreCase("dump off")) {
      dump_all = false;
      Serial.println("[CAN] Dump OFF");
    } else if (line.startsWith("send")) {
      uint16_t id = 0;
      uint8_t data[8] = {0};
      uint8_t len = 0;
      if (parseCanSendLine_(line, id, data, len)) {
        CAN_message_t msg;
        msg.id = id;
        msg.len = len;
        msg.flags.extended = 0;
        msg.flags.remote = 0;
        for (uint8_t i = 0; i < len; ++i) {
          msg.buf[i] = data[i];
        }
        if (Can1.write(msg)) {
          Serial.print("TX id=0x");
          Serial.print(id, HEX);
          Serial.print(" len=");
          Serial.println(len);
        } else {
          Serial.println("TX fail");
        }
      } else {
        Serial.println("TX parse fail");
      }
    } else {
      float pct = 0.0f;
      uint8_t target_node = 0;
      bool send_all = true;
      if (parseOpenPercent(line, pct, target_node, send_all)) {
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        int32_t cp = (int32_t)lroundf(pct * 100.0f); // centi-percent
        if (cp > INT16_MAX) cp = INT16_MAX;
        if (cp < 0) cp = 0;
        bool all_ok = true;
        for (uint8_t i = 0; i < kNodeCount; ++i) {
          if (!send_all && kNodeIds[i] != target_node) {
            continue;
          }
          CAN_message_t msg;
          msg.id = kCmdBaseId + kNodeIds[i];
          msg.len = 2;
          msg.flags.extended = 0;
          msg.flags.remote = 0;
          int16_t cp_to_send = (int16_t)cp;
          msg.buf[0] = (uint8_t)(cp_to_send & 0xFF);
          msg.buf[1] = (uint8_t)((cp_to_send >> 8) & 0xFF);

          if (!Can1.write(msg)) {
            all_ok = false;
          }
        }
        if (all_ok) {
          Serial.print("TX pct=");
          Serial.println(pct, 2);
          have_cmd = true;
          last_cmd_pct = pct;
          last_send_all = send_all;
          last_target_node = target_node;
          last_tx_ms = millis();
        } else {
          Serial.println("TX fail");
        }
      }
    }
  }

  // TX: repeat last command to avoid receiver timeout
  if (have_cmd) {
    const uint32_t now_ms = millis();
    if ((uint32_t)(now_ms - last_tx_ms) >= kCmdRepeatMs) {
      last_tx_ms = now_ms;
      int32_t cp = (int32_t)lroundf(last_cmd_pct * 100.0f);
      if (cp > INT16_MAX) cp = INT16_MAX;
      if (cp < 0) cp = 0;
      for (uint8_t i = 0; i < kNodeCount; ++i) {
        if (!last_send_all && kNodeIds[i] != last_target_node) {
          continue;
        }
        CAN_message_t msg;
        msg.id = kCmdBaseId + kNodeIds[i];
        msg.len = 2;
        msg.flags.extended = 0;
        msg.flags.remote = 0;
        int16_t cp_to_send = (int16_t)cp;
        msg.buf[0] = (uint8_t)(cp_to_send & 0xFF);
        msg.buf[1] = (uint8_t)((cp_to_send >> 8) & 0xFF);
        Can1.write(msg);
      }
    }
  }

  // RX: drain all pending position frames
  CAN_message_t msg;
  while (Can1.read(msg)) {
    if (msg.flags.extended || msg.len < 2) {
      continue;
    }

    if (dump_all) {
      Serial.print("DUMP id=0x");
      Serial.print(msg.id, HEX);
      Serial.print(" len=");
      Serial.print(msg.len);
      Serial.print(" data=");
      for (uint8_t i = 0; i < msg.len && i < 8; ++i) {
        if (i) Serial.print(' ');
        if (msg.buf[i] < 0x10) Serial.print('0');
        Serial.print(msg.buf[i], HEX);
      }
      Serial.println();
    }

    uint8_t node_id = 0;
    if (!findNodeFromId((uint16_t)msg.id, node_id)) {
      continue;
    }

    if (rx_print) {
      int16_t cp_rx = (int16_t)((uint16_t)msg.buf[0] | ((uint16_t)msg.buf[1] << 8));
      float pct = cp_rx * 0.01f;
      if (pct < 0.0f) pct = 0.0f;
      if (pct > 100.0f) pct = 100.0f;

      Serial.print("POS id=0x");
      Serial.print(msg.id, HEX);
      Serial.print(" node=");
      Serial.print(node_id);
      Serial.print(" pct=");
      Serial.println(pct, 2);
    }
  }
}
