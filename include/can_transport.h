#pragma once
#include <Arduino.h>

// CAN transport layer: hardware access and raw RX frames.
namespace CanTransport {

  struct RxFrame {
    uint16_t std_id = 0;
    uint8_t  dlc = 0;
    uint8_t  data[8] = {0};
  };

  bool begin1Mbps();
  bool poll(RxFrame& out);
  bool sendStd(uint16_t std_id, const uint8_t data[8], uint8_t len);

}
