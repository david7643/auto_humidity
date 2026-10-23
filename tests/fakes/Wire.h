#pragma once
#include "Arduino.h"
struct WireFake {
  std::vector<uint8_t> data;
  uint8_t error = 0;
  void begin() {}
  void setWireTimeout(uint32_t, bool) {}
  void beginTransmission(uint8_t) {}
  void write(uint8_t) {}
  int endTransmission() { return error; }
  int requestFrom(uint8_t, uint8_t) { return int(data.size()); }
  int available() { return int(data.size()); }
  int read() { int b=data.front(); data.erase(data.begin()); return b; }
};
inline WireFake Wire;
