#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#define __attribute__(x)
#define F(x) x
#define PSTR(x) x
#define PROGMEM
#define strcmp_P strcmp
#define strcasecmp_P _stricmp
#define memcpy_P memcpy
#define OUTPUT 1
#define HIGH 1
using std::min;
template<class T, class A, class B> T constrain(T v, A lo, B hi) { return std::max(T(lo), std::min(v, T(hi))); }
inline uint32_t fakeNow = 0;
inline uint32_t millis() { return fakeNow; }
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
class Print {
 public:
  virtual size_t write(uint8_t c) = 0;
  size_t write(const uint8_t *p, size_t n) { for (size_t i=0;i<n;++i) write(p[i]); return n; }
  void print(const char *p) { while (*p) write(*p++); }
  void print(char c) { write(c); }
  template<class T> void print(T n) { print(std::to_string(n).c_str()); }
  template<class T> void println(T v) { print(v); print('\n'); }
};
struct SerialFake : Print {
  void begin(int) {}
  size_t write(uint8_t) override { return 1; }
};
inline SerialFake Serial;
