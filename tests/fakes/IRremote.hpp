#pragma once
#include "Arduino.h"
constexpr uint8_t NEC=7, NEC2=8, ONKYO=9, SAMSUNG=22, SONY=26;
constexpr uint8_t IRDATA_FLAGS_IS_REPEAT=1, IRDATA_FLAGS_IS_AUTO_REPEAT=2;
constexpr uint8_t IRDATA_FLAGS_WAS_OVERFLOW=64, IRDATA_FLAGS_PARITY_FAILED=4;
constexpr int MICROS_PER_TICK=50, MARK_EXCESS_MICROS=20;
inline int fakeSends=0;
struct ReceiverFake {
  struct { uint8_t protocol=NEC, flags=0, rawlen=0; uint16_t address=0, command=0, numberOfBits=32; } decodedIRData;
  struct { uint8_t rawbuf[104]; } irparams;
  bool pending=false;
  void begin(int, bool) {}
  void stop() {}
  void start() {}
  void resume() { pending=false; }
  bool decode() { return pending; }
};
struct SenderFake {
  void begin(int) {}
  void sendRaw(const uint16_t*, uint8_t, uint8_t) { ++fakeSends; }
  void sendNEC(uint16_t,uint16_t,int) { ++fakeSends; }
  void sendNEC2(uint16_t,uint16_t,int) { ++fakeSends; }
  void sendOnkyo(uint16_t,uint16_t,int) { ++fakeSends; }
  void sendSamsung(uint16_t,uint16_t,int) { ++fakeSends; }
  void sendSony(uint16_t,uint16_t,int,int) { ++fakeSends; }
};
inline ReceiverFake IrReceiver;
inline SenderFake IrSender;
