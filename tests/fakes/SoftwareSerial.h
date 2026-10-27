#pragma once
#include "Arduino.h"
#include <deque>
class SoftwareSerial {
 public:
  std::deque<uint8_t> input;
  std::vector<uint8_t> output;
  SoftwareSerial(uint8_t, uint8_t) {}
  void begin(long) {}
  bool listening = true;
  bool listen() { listening = true; return true; }
  bool stopListening() { listening = false; return true; }
  int available() { return int(input.size()); }
  int read() { int b = input.front(); input.pop_front(); return b; }
  size_t write(const uint8_t *bytes, size_t n) {
    output.insert(output.end(), bytes, bytes + n); return n;
  }
};
