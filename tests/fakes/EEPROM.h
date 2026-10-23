#pragma once
#include "Arduino.h"
struct EepromFake {
  uint8_t data[1024];
  EepromFake() { memset(data, 0xFF, sizeof(data)); }
  template<class T> void get(int a, T &v) { memcpy(&v, data+a, sizeof(v)); }
  void update(int a, uint8_t b) { data[a]=b; }
};
inline EepromFake EEPROM;
