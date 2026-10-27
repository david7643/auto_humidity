#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>
#include "Config.h"
#include "Control.h"
#include "Storage.h"
#include "Firmware.h"
#include "WebPage.h"

// Uno D13 is SPI SCK, so IRremote must never use its feedback LED.
#define NO_LED_FEEDBACK_CODE
#define RAW_BUFFER_LENGTH 104
#define DECODE_NEC
#define DECODE_SAMSUNG
#define DECODE_SONY
#if HUMIDIFIER_EXTENDED_IR
#define DECODE_RC5
#define DECODE_RC6
#define DECODE_PANASONIC
#define DECODE_JVC
#define DECODE_LG
#endif
#include <IRremote.hpp>

namespace {
SoftwareSerial co2Serial(Config::CO2_RX, Config::CO2_TX);
uint16_t co2Ppm = 0;
uint32_t co2StartedAt = 0, co2RequestAt = 0, co2SampleAt = 0;
uint8_t co2Frame[9], co2Used = 0;
bool co2Waiting = false, co2HasSample = false, co2Warm = false;
// Status: 0 warming, 1 waiting/error, 2 fresh. Never used by humidity control.
uint8_t co2Status(uint32_t now) {
  if (!co2Warm) return 0;
  return co2HasSample && uint32_t(now - co2SampleAt) < 15000UL ? 2 : 1;
}
void co2Tick(uint32_t now) {
  if (!co2Warm) {
    if (uint32_t(now - co2StartedAt) < Config::CO2_WARMUP_MS) return;
    co2Warm = true;
    co2RequestAt = now - Config::CO2_POLL_MS;
  }
  // Consume only buffered bytes; never wait for a UART response.
  uint8_t budget = 64;
  while (budget-- && co2Serial.available()) {
    uint8_t b = co2Serial.read();
    if (!co2Waiting) continue;
    if (!co2Used && b != 0xFF) continue;
    if (co2Used == 1 && b != 0x86) { co2Used = b == 0xFF ? 1 : 0; continue; }
    co2Frame[co2Used++] = b;
    if (co2Used == 9) {
      uint8_t sum = 0;
      for (uint8_t i = 1; i < 9; ++i) sum += co2Frame[i];
      uint16_t ppm = uint16_t(co2Frame[2]) * 256 + co2Frame[3];
      if (!sum && ppm >= 400 && ppm <= 10000) {
        co2Ppm = ppm; co2SampleAt = now; co2HasSample = true; co2Waiting = false;
      }
      co2Used = 0;
    }
  }
  if (co2Waiting && uint32_t(now - co2RequestAt) >= 1000UL) {
    co2Waiting = false; co2Used = 0;
  }
  if (!co2Waiting && uint32_t(now - co2RequestAt) >= Config::CO2_POLL_MS) {
    const uint8_t command[] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
    co2Used = 0; co2RequestAt = now; co2Waiting = true;
    co2Serial.write(command, sizeof(command));
  }
}
Settings settings;
uint8_t settingsSlot = 0;
IrRecord ir; // Shared EEPROM/learning scratch buffer, not three RAM copies.
bool saved[3] = {};
uint8_t learnState = 0, learnSlot = 0; // idle, waiting, candidate, timeout, rejected
bool tested = false;
uint32_t learnAt = 0, lastIrAt = 0;
uint32_t revision = 1, powerAt = 0, levelAt = 0, syncedAt = 0;
uint32_t irCount = 0, powerCount = 0, levelCount = 0;
Power power = POWER_UNKNOWN;
uint8_t level = 0, source = 0; // source: unknown / user / IR inference
Mode mode = PAUSED;
uint8_t rememberedLevel = 0, sleepState = 0, resumeLevel = 0; // sleep: 0 unknown, 1 off, 2 on
void forgetState() { power = POWER_UNKNOWN; level = source = rememberedLevel = sleepState = resumeLevel = 0; }
struct Event { uint32_t seconds; uint8_t kind, argument; };
Event events[Config::EVENT_COUNT];
uint8_t eventHead = 0, eventSize = 0;
void event(uint8_t kind, uint8_t arg = 0) {
  events[eventHead] = {millis() / 1000UL, kind, arg};
  eventHead = (eventHead + 1) % Config::EVENT_COUNT;
  if (eventSize < Config::EVENT_COUNT) ++eventSize;
}
void pauseControl() { mode = PAUSED; }
void persist() {
  ++settings.sequence;
  settingsSlot ^= 1;
  writeRecord(settingsSlot * sizeof(Settings), settings);
}
void loadSettings() {
  Settings other;
  bool a = readRecord(0, settings), b = readRecord(sizeof(Settings), other);
  if (b && (!a || int16_t(other.sequence - settings.sequence) > 0)) {
    settings = other; settingsSlot = 1;
  } else if (!a) settings = {SCHEMA, 45, 0, 0, 0, 0, 0};
  if (settings.target < 40 || settings.target > 47 ||
      settings.autoWanted > 1)
    settings = {SCHEMA, 45, 0, 0, 0, 0, 0};
  for (uint8_t i = 0; i < 3; ++i)
    saved[i] = readRecord(IR_BASE + i * sizeof(IrRecord), ir);
}

int16_t temperature = 0, humidity = 0, filtered = 0, trend = 0, trendBase = 0;
int32_t ema = 0;
bool hasSample = false, sampleOK = false, converting = false, sensorFault = false;
uint8_t sampleCount = 0, highCount = 0;
uint32_t sampleAt = 0, sensorAt = 0, conversionAt = 0, trendAt = 0;
uint16_t sensorErrors = 0;
uint8_t sensorCrc(const uint8_t *bytes) {
  uint8_t c = 0xFF;
  for (uint8_t i = 0; i < 2; ++i) {
    c ^= bytes[i];
    for (uint8_t b = 0; b < 8; ++b) c = c & 0x80 ? (c << 1) ^ 0x31 : c << 1;
  }
  return c;
}
bool sensorReady(uint32_t now) {
  return sampleOK && hasSample && sampleCount >= 10 &&
    uint32_t(now - sampleAt) < Config::SENSOR_STALE_MS;
}
void sensorFailure() {
  sampleOK = false; highCount = 0;
  if (sensorErrors != UINT16_MAX) ++sensorErrors;
}
void sensorTick(uint32_t now) {
  if (converting && uint32_t(now - conversionAt) >= 10) {
    converting = false;
    uint8_t b[6];
    uint8_t received = Wire.requestFrom(Config::SHT_ADDRESS, uint8_t(6));
    if (received != 6) {
#if HUMIDIFIER_SERIAL_DEBUG
      if (sensorErrors < 3) { Serial.print(F("SHT45 received bytes: ")); Serial.println(received); }
#endif
      while (Wire.available()) Wire.read();
      sensorFailure();
    } else {
      for (uint8_t i = 0; i < 6; ++i) b[i] = Wire.read();
      if (sensorCrc(b) != b[2] || sensorCrc(b + 3) != b[5]) {
#if HUMIDIFIER_SERIAL_DEBUG
        if (sensorErrors < 3) Serial.println(F("SHT45 CRC error"));
#endif
        sensorFailure();
      }
      else {
        temperature = int32_t((uint32_t(b[0]) << 8 | b[1]) * 17500UL / 65535UL) - 4500;
        int32_t h = int32_t((uint32_t(b[3]) << 8 | b[4]) * 12500UL / 65535UL) - 600;
        humidity = constrain(h, 0L, 10000L);
        if (!hasSample || uint32_t(now - sampleAt) >= Config::SENSOR_STALE_MS) {
          ema = int32_t(humidity) * 256;
          sampleCount = 0; trendBase = humidity; trendAt = now; trend = 0;
        } else ema += (int32_t(humidity) * 256 - ema) / 60;
        filtered = ema / 256;
        if (sampleCount < 60) ++sampleCount;
        highCount = humidity >= 5000 ? min(uint8_t(3), uint8_t(highCount + 1)) : 0;
        sampleAt = now; sampleOK = hasSample = true;
        if (uint32_t(now - trendAt) >= 60000UL) {
          trend = filtered - trendBase; trendBase = filtered; trendAt = now;
        }
      }
    }
  }
  if (!converting && uint32_t(now - sensorAt) >= 1000) {
    sensorAt = now;
    Wire.beginTransmission(Config::SHT_ADDRESS); Wire.write(0xFD);
    uint8_t result = Wire.endTransmission();
    if (result == 0) { converting = true; conversionAt = now; }
    else {
#if HUMIDIFIER_SERIAL_DEBUG
      if (sensorErrors < 3) { Serial.print(F("SHT45 command error: ")); Serial.println(result); }
#endif
      sensorFailure();
    }
  }
  if ((!hasSample && now >= Config::SENSOR_STALE_MS) ||
      (hasSample && uint32_t(now - sampleAt) >= Config::SENSOR_STALE_MS)) {
    if (!sensorFault) { sensorFault = true; pauseControl(); ++revision; event(5); }
    sampleCount = 0;
  } else if (sensorFault && sensorReady(now)) {
    sensorFault = false; event(6); // No automatic resume after a long outage.
  }
}
bool compactProtocol(uint8_t p) {
  return p == NEC || p == NEC2 || p == ONKYO || p == SAMSUNG || p == SONY
#if HUMIDIFIER_EXTENDED_IR
    || p == RC5 || p == RC6 || p == PANASONIC || p == JVC || p == LG
#endif
    ;
}
bool validIr() {
  if (ir.schema != SCHEMA || ir.repeats > 3 || ir.khz < 30 || ir.khz > 60) return false;
  if (ir.length) {
    if (ir.length > Config::RAW_MAX) return false;
    uint32_t duration = 0;
    for (uint8_t i = 0; i < ir.length; ++i)
      if (!ir.raw[i] || ir.raw[i] > 12000) return false;
      else duration += ir.raw[i];
    return duration <= 200000UL;
  }
  return compactProtocol(ir.protocol);
}
bool transmit(uint32_t now) {
  if (uint32_t(now - lastIrAt) < Config::IR_GAP_MS || !validIr()) return false;
  IrReceiver.stop();
  if (ir.length) IrSender.sendRaw(ir.raw, ir.length, ir.khz);
  else switch (ir.protocol) {
    case NEC: IrSender.sendNEC(ir.address, ir.command, ir.repeats); break;
    case NEC2: IrSender.sendNEC2(ir.address, ir.command, ir.repeats); break;
    case ONKYO: IrSender.sendOnkyo(ir.address, ir.command, ir.repeats); break;
    case SAMSUNG: IrSender.sendSamsung(ir.address, ir.command, ir.repeats); break;
    case SONY: IrSender.sendSony(ir.address, ir.command, ir.repeats, ir.bits); break;
#if HUMIDIFIER_EXTENDED_IR
    case RC5: IrSender.sendRC5(ir.address, ir.command, ir.repeats, true); break;
    case RC6: IrSender.sendRC6(ir.address, ir.command, ir.repeats, true); break;
    case PANASONIC: IrSender.sendPanasonic(ir.address, ir.command, ir.repeats); break;
    case JVC: IrSender.sendJVC(uint8_t(ir.address), uint8_t(ir.command), ir.repeats); break;
    case LG: IrSender.sendLG(ir.address, ir.command, ir.repeats); break;
#endif
    default: IrReceiver.start(); return false;
  }
  IrReceiver.start(); IrReceiver.resume();
  lastIrAt = millis(); ++irCount;
  return true;
}
void infer(Command command, uint32_t now) {
  if (command == POWER_COMMAND) {
    if (power == ON && sleepState == 2) {
      // First power press exits sleep. It does NOT switch the humidifier off.
      sleepState = 1; level = rememberedLevel = resumeLevel; resumeLevel = 0;
      levelAt = now;
    } else if (power == ON && sleepState == 1) {
      rememberedLevel = level; power = OFF; level = 0;
      powerAt = levelAt = now;
    } else if (power == ON) {
      forgetState(); // Could have exited sleep or powered off; cannot infer which.
    } else if (power == OFF) {
      power = ON;
      level = sleepState == 1 ? rememberedLevel : 0;
      powerAt = levelAt = now;
    }
    ++powerCount;
  } else if (power == ON && command == LEVEL_COMMAND) {
    if (sleepState == 1) {
      if (level) level = level == 1 ? 2 : 1;
      rememberedLevel = level;
    } else level = rememberedLevel = sleepState = resumeLevel = 0;
    levelAt = now; ++levelCount;
  } else if (power == ON && command == SLEEP_COMMAND) {
    if (sleepState == 1) {
      resumeLevel = level; sleepState = 2; level = 1;
    } else if (sleepState == 2) {
      sleepState = 1; level = rememberedLevel = resumeLevel; resumeLevel = 0;
    } else level = rememberedLevel = resumeLevel = 0;
    levelAt = now; ++levelCount;
  } else forgetState();
  source = power == POWER_UNKNOWN ? 0 : 2;
  ++revision; event(3, command);
}
bool sendSaved(uint8_t slot, uint32_t now) {
  if (slot > 2 || !saved[slot] || learnState == 1 || learnState == 2) return false;
  if (!readRecord(IR_BASE + slot * sizeof(IrRecord), ir) || !transmit(now)) return false;
  infer(Command(slot), millis());
  return true;
}
void learnTick(uint32_t now) {
  if (learnState == 1 && uint32_t(now - learnAt) >= 15000UL) {
    learnState = 3; ++revision; event(8);
  }
  if (!IrReceiver.decode()) return;
  if (learnState == 1) {
    auto &data = IrReceiver.decodedIRData;
    if (!(data.flags & (IRDATA_FLAGS_IS_REPEAT | IRDATA_FLAGS_IS_AUTO_REPEAT))) {
      memset(&ir, 0, sizeof(ir)); tested = false;
      ir.schema = SCHEMA; ir.protocol = data.protocol;
      ir.address = data.address; ir.command = data.command;
      ir.bits = data.numberOfBits; ir.khz = 38;
      ir.repeats = data.protocol == SONY ? 2 : 0;
      bool ok = !(data.flags & (IRDATA_FLAGS_WAS_OVERFLOW | IRDATA_FLAGS_PARITY_FAILED));
      if (!compactProtocol(ir.protocol) && ok) {
        if (data.rawlen < 5 || data.rawlen - 1 > Config::RAW_MAX) ok = false;
        else {
          ir.length = data.rawlen - 1;
          for (uint8_t i = 0; i < ir.length; ++i) {
            int32_t us = int32_t(IrReceiver.irparams.rawbuf[i + 1]) * MICROS_PER_TICK;
            us += (i & 1) ? MARK_EXCESS_MICROS : -MARK_EXCESS_MICROS;
            ir.raw[i] = us > 0 ? us : 1;
          }
        }
      }
      learnState = ok && validIr() ? 2 : 4;
      forgetState();
      ++revision; event(7, learnState);
    }
  }
  // External remote frames outside learning are not treated as device feedback.
  IrReceiver.resume();
}
uint32_t controlAt = 0;
void controlTick(uint32_t now) {
  if (uint32_t(now - controlAt) < 5000UL) return;
  controlAt = now;
  if (learnState == 1 || learnState == 2) return;
  ControlInput input = {now, powerAt, levelAt, filtered, trend, settings.target, level,
    rememberedLevel, power, mode == AUTO, sensorReady(now), highCount >= 3,
    saved[0], saved[1], sleepState == 1};
  Command command = decide(input);
  if (command != NO_COMMAND && !sendSaved(command, now)) {
    pauseControl(); ++revision; event(9);
  }
}

EthernetServer server(80);
EthernetClient client;
char io[192];
enum Route : uint8_t { NONE, PAGE, STATUS, EVENTS, TARGET, MODE_ROUTE, SYNC,
  SEND, LEARN, LEARN_STATUS, TEST, SAVE, CANCEL, ERASE, PROFILE };
uint8_t netPhase = 0;
Route route = NONE;
uint16_t used = 0, bodyLength = 0, headerBytes = 0;
uint32_t connectedAt = 0;
bool post = false, contentType = false, lengthSeen = false;
size_t pageOffset = 0;
// Reuse request memory to batch small Print calls into socket writes.
class Reply : public Print {
  uint8_t count = 0;
 public:
  using Print::write;
  size_t write(uint8_t b) override {
    io[count++] = b;
    if (count == 64) flush();
    return 1;
  }
  void flush() {
    if (count) {
      // Never enter Ethernet.socketSend()'s wait-for-free-space loop.
      if (client.availableForWrite() >= count)
        client.write(reinterpret_cast<const uint8_t *>(io), count);
      else client.stop();
      count = 0;
    }
  }
};
void closeClient() { client.stop(); }
void headers(Reply &out, bool json = true) {
  out.print(F("HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Type: "));
  if (json) out.print(F("application/json\r\n\r\n"));
  else out.print(F("text/html; charset=utf-8\r\nContent-Encoding: gzip\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'\r\n\r\n"));
}
void failure(uint16_t code) {
  Reply out;
  out.print(F("HTTP/1.1 ")); out.print(code);
  out.print(F(" Error\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n{\"error\":"));
  out.print(code); out.print('}'); out.flush(); closeClient();
}
void success() {
  Reply out; headers(out); out.print(F("{\"ok\":true}")); out.flush(); closeClient();
}
void statusReply() {
  Reply out; headers(out);
  out.print(F("{\"rev\":")); out.print(revision);
  out.print(F(",\"t\":")); out.print(temperature);
  out.print(F(",\"h\":")); out.print(humidity);
  out.print(F(",\"co2\":")); out.print(co2Ppm);
  out.print(F(",\"co2Status\":")); out.print(co2Status(millis()));
  out.print(F(",\"avg\":")); out.print(filtered);
  out.print(F(",\"trend\":")); out.print(trend);
  out.print(F(",\"valid\":")); out.print(sampleOK && hasSample ? 1 : 0);
  out.print(F(",\"ready\":")); out.print(sensorReady(millis()) ? 1 : 0);
  out.print(F(",\"age\":")); out.print(hasSample ? millis() - sampleAt : UINT32_MAX);
  out.print(F(",\"at\":")); out.print(millis() / 1000UL);
  out.print(F(",\"target\":")); out.print(settings.target);
  out.print(F(",\"p\":")); out.print(power);
  out.print(F(",\"l\":")); out.print(level);
  out.print(F(",\"m\":")); out.print(mode);
  out.print(F(",\"wanted\":")); out.print(settings.autoWanted);
  out.print(F(",\"src\":")); out.print(source);
  out.print(F(",\"sync\":")); out.print(syncedAt / 1000UL);
  out.print(F(",\"fault\":")); out.print(sensorFault ? 1 : 0);
  out.print(F(",\"high\":")); out.print(highCount >= 3 ? 1 : 0);
  out.print(F(",\"errors\":")); out.print(sensorErrors);
  out.print(F(",\"counts\":[")); out.print(irCount); out.print(',');
  out.print(powerCount); out.print(','); out.print(levelCount);
  out.print(F("],\"saved\":["));
  for (uint8_t i = 0; i < 3; ++i) { if (i) out.print(','); out.print(saved[i] ? 1 : 0); }
  out.print(F("],\"learn\":")); out.print(learnState);
  out.print(F(",\"slot\":")); out.print(learnSlot);
  out.print(F(",\"proto\":")); out.print(learnState == 2 ? ir.protocol : 0);
  out.print(F(",\"bits\":")); out.print(learnState == 2 ? ir.bits : 0);
  out.print(F(",\"raw\":")); out.print(learnState == 2 ? ir.length : 0);
  out.print(F(",\"tested\":")); out.print(tested ? 1 : 0);
  out.print(F(",\"remembered\":")); out.print(rememberedLevel);
  out.print(F(",\"resume\":")); out.print(resumeLevel);
  out.print(F(",\"sleep\":")); out.print(sleepState);
  out.print('}'); out.flush(); closeClient();
}
void eventsReply() {
  Reply out; headers(out); out.print('[');
  for (uint8_t i = 0; i < eventSize; ++i) {
    const Event &e = events[(eventHead + Config::EVENT_COUNT - eventSize + i) % Config::EVENT_COUNT];
    if (i) out.print(',');
    out.print('['); out.print(e.seconds); out.print(','); out.print(e.kind);
    out.print(','); out.print(e.argument); out.print(']');
  }
  out.print(']'); out.flush(); closeClient();
}
bool number(const char *text, uint32_t &value) {
  if (!*text) return false;
  value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9' || value > (UINT32_MAX - (*text - '0')) / 10UL) return false;
    value = value * 10 + *text - '0';
  }
  return true;
}
// Strict decimal fields, no allocation and no ambiguous duplicate keys.
bool fields(uint32_t &r, int16_t &a, int16_t &b, int16_t &c, int16_t &d) {
  uint8_t seen = 0;
  a = b = c = d = -1;
  char *cursor = io;
  while (*cursor) {
    char key = cursor[0];
    if (!key || cursor[1] != '=') return false;
    char *value = cursor + 2, *end = strchr(value, '&');
    if (end) *end = '\0';
    uint32_t n;
    if (!number(value, n)) return false;
    uint8_t bit = key == 'r' ? 1 : key == 'a' ? 2 : key == 'b' ? 4 : key == 'c' ? 8 : key == 'd' ? 16 : 0;
    if (!bit || (seen & bit) || (key != 'r' && n > 32767)) return false;
    seen |= bit;
    if (key == 'r') r = n;
    else if (key == 'a') a = n;
    else if (key == 'b') b = n;
    else if (key == 'c') c = n;
    else d = n;
    if (!end) break;
    cursor = end + 1;
    if (!*cursor) return false;
  }
  return seen & 1;
}
void handlePost() {
  uint32_t expected = 0;
  int16_t a, b, c, d;
  if (!fields(expected, a, b, c, d)) { failure(400); return; }
  if (expected != revision) { failure(409); return; }
  const uint32_t now = millis();
  switch (route) {
    case TARGET:
      if (a < 40 || a > 47) { failure(400); return; }
      if (settings.target != a) { settings.target = a; persist(); }
      event(10, a); break;
    case MODE_ROUTE:
      if (a < MANUAL || a > PAUSED) { failure(400); return; }
      if (a == AUTO && (power == POWER_UNKNOWN || !sensorReady(now) || sensorFault ||
          learnState == 1 || learnState == 2 || !saved[0] ||
          sleepState != 1 || (power == OFF && !rememberedLevel))) {
        failure(409); return;
      }
      mode = Mode(a);
      if (a != PAUSED && settings.autoWanted != (a == AUTO)) {
        settings.autoWanted = a == AUTO; persist();
      }
      event(2, a); break;
    case SYNC:
      if (a < POWER_UNKNOWN || a > ON || b < 0 || b > 2 || c < 0 || c > 2 || d < 0 || d > 2 ||
          (a == POWER_UNKNOWN && (b || c || d)) || (c != 2 && d) ||
          (a == OFF && c == 2) || (a == ON && c == 2 && b != 1)) {
        failure(400); return;
      }
      // Explicit sleep fields prevent old clients silently confirming sleep OFF.
      pauseControl(); power = Power(a); level = a == ON ? b : 0;
      sleepState = c; resumeLevel = d;
      rememberedLevel = c == 1 ? b : 0;
      source = a == POWER_UNKNOWN ? 0 : 1;
      syncedAt = powerAt = levelAt = now;
      event(1, a * 3 + b); break;
    case SEND:
      if (a < 0 || a > 2) { failure(400); return; }
      pauseControl();
      if (!sendSaved(a, now)) { ++revision; failure(409); return; }
      success(); return; // infer() has already advanced revision.
    case LEARN:
      if (a < 0 || a > 2) { failure(400); return; }
      pauseControl(); learnSlot = a; learnState = 1; learnAt = now; tested = false;
      forgetState();
      IrReceiver.resume(); event(4, a); break;
    case CANCEL:
      learnState = 0; tested = false; pauseControl(); break;
    case TEST:
      if (learnState != 2 || a < 0 || a > 3 || b < 30 || b > 60 ||
          (ir.length && a != 0)) { failure(400); return; }
      ir.repeats = a; ir.khz = b; tested = false; pauseControl();
      if (!transmit(now)) { ++revision; failure(409); return; }
      forgetState(); tested = true; event(11, learnSlot); break;
    case SAVE:
      if (learnState != 2 || !tested || a != 1) { failure(409); return; }
      writeRecord(IR_BASE + learnSlot * sizeof(IrRecord), ir);
      saved[learnSlot] = true; learnState = 0; tested = false; event(12, learnSlot); break;
    case ERASE:
      if (a < 0 || a > 2) { failure(400); return; }
      pauseControl(); EEPROM.update(IR_BASE + a * sizeof(IrRecord), 0);
      saved[a] = false; event(13, a); break;
    case PROFILE:
      failure(410); return; // Fixed startup level / idempotent sleep assumptions removed.
    default: failure(404); return;
  }
  ++revision; success();
}
Route lookup(const char *path) {
  if (!strcmp_P(path, PSTR("/"))) return PAGE;
  if (!strcmp_P(path, PSTR("/api/status"))) return STATUS;
  if (!strcmp_P(path, PSTR("/api/events"))) return EVENTS;
  if (!strcmp_P(path, PSTR("/api/target"))) return TARGET;
  if (!strcmp_P(path, PSTR("/api/mode"))) return MODE_ROUTE;
  if (!strcmp_P(path, PSTR("/api/state/sync"))) return SYNC;
  if (!strcmp_P(path, PSTR("/api/ir/send"))) return SEND;
  if (!strcmp_P(path, PSTR("/api/ir/learn"))) return LEARN;
  if (!strcmp_P(path, PSTR("/api/ir/status"))) return LEARN_STATUS;
  if (!strcmp_P(path, PSTR("/api/ir/test"))) return TEST;
  if (!strcmp_P(path, PSTR("/api/ir/save"))) return SAVE;
  if (!strcmp_P(path, PSTR("/api/ir/cancel"))) return CANCEL;
  if (!strcmp_P(path, PSTR("/api/ir/delete"))) return ERASE;
  if (!strcmp_P(path, PSTR("/api/profile"))) return PROFILE;
  return NONE;
}
void dispatch() {
  if (post) { handlePost(); return; }
  if (route == PAGE) {
    Reply out; headers(out, false); out.flush(); netPhase = 3; pageOffset = 0;
  } else if (route == STATUS || route == LEARN_STATUS) statusReply();
  else if (route == EVENTS) eventsReply();
  else failure(405);
}
void lineReady() {
  io[used] = '\0';
  if (netPhase == 0) {
    char *path = strchr(io, ' ');
    if (!path) { failure(400); return; }
    *path++ = '\0'; post = !strcmp_P(io, PSTR("POST"));
    if (!post && strcmp_P(io, PSTR("GET"))) { failure(405); return; }
    char *version = strchr(path, ' ');
    if (!version) { failure(400); return; }
    *version++ = '\0';
    if (strcmp_P(version, PSTR("HTTP/1.1")) && strcmp_P(version, PSTR("HTTP/1.0"))) {
      failure(400); return;
    }
    route = lookup(path);
    if (route == NONE) { failure(404); return; }
    netPhase = 1;
  } else if (!used) {
    if (post) {
      if (!contentType || !lengthSeen || !bodyLength || bodyLength >= sizeof(io)) {
        failure(400); return;
      }
      netPhase = 2;
    } else dispatch();
  } else {
    char *value = strchr(io, ':');
    if (!value) { failure(400); return; }
    *value++ = '\0'; while (*value == ' ' || *value == '\t') ++value;
    if (!strcasecmp_P(io, PSTR("Content-Length"))) {
      uint32_t n;
      if (lengthSeen || !number(value, n) || n >= sizeof(io)) { failure(413); return; }
      bodyLength = n; lengthSeen = true;
    } else if (!strcasecmp_P(io, PSTR("Content-Type")))
      contentType = !strcmp_P(value, PSTR("application/x-www-form-urlencoded"));
    else if (!strcasecmp_P(io, PSTR("Transfer-Encoding"))) { failure(400); return; }
  }
  used = 0;
}
void networkTick(uint32_t now) {
  if (!client) {
    client = server.accept();
    if (!client) return;
    client.setConnectionTimeout(100);
    connectedAt = now; netPhase = 0; used = bodyLength = headerBytes = 0;
    contentType = lengthSeen = post = false; route = NONE;
  }
  if (uint32_t(now - connectedAt) >= 4000UL) { closeClient(); return; }
  if (netPhase == 3) {
    if (client.availableForWrite() < 64) return;
    size_t remaining = sizeof(WEB_PAGE) - pageOffset;
    uint8_t count = remaining < 64 ? remaining : 64;
    memcpy_P(io, WEB_PAGE + pageOffset, count);
    client.write(reinterpret_cast<const uint8_t *>(io), count);
    pageOffset += count;
    if (pageOffset == sizeof(WEB_PAGE)) closeClient();
    return;
  }
  uint8_t budget = 64;
  while (budget-- && client && client.available()) {
    int ch = client.read();
    if (ch < 0) break;
    if (netPhase == 2) {
      if (ch < 32 || ch > 126) { failure(400); return; }
      io[used++] = ch;
      if (used == bodyLength) { io[used] = '\0'; dispatch(); return; }
      continue;
    }
    if (++headerBytes > 1200) { failure(431); return; }
    if (ch == '\r') continue;
    if (ch == '\n') { lineReady(); if (!client || netPhase == 3) return; }
    else {
      if (ch < 32 || ch > 126 || used >= sizeof(io) - 1) { failure(431); return; }
      io[used++] = ch;
    }
  }
  if (client && !client.connected() && !client.available()) closeClient();
}
} // namespace
void firmwareBegin() {
#if HUMIDIFIER_SERIAL_DEBUG
  Serial.begin(9600);
#endif
  co2Serial.begin(9600); co2StartedAt = millis();
  pinMode(Config::SD_CS, OUTPUT); digitalWrite(Config::SD_CS, HIGH);
  loadSettings();
  Wire.begin(); Wire.setWireTimeout(25000, true);
#if HUMIDIFIER_SERIAL_DEBUG
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      ++found;
      Serial.print(F("I2C address (decimal): ")); Serial.println(address);
    }
  }
  if (!found) Serial.println(F("No I2C device: check SHT45 power, SDA=A4, SCL=A5."));
#endif
  IrReceiver.begin(Config::IR_RX, false); IrSender.begin(Config::IR_TX);
  Ethernet.init(Config::ETH_CS);
  uint8_t mac[6]; memcpy(mac, Config::MAC, sizeof(mac));
  Ethernet.begin(mac, IPAddress(Config::IP), IPAddress(Config::GATEWAY),
    IPAddress(Config::GATEWAY), IPAddress(Config::SUBNET));
  Ethernet.setRetransmissionTimeout(100); Ethernet.setRetransmissionCount(1);
  server.begin(); powerAt = levelAt = millis(); event(0);
#if HUMIDIFIER_SERIAL_DEBUG
  Serial.print(F("Smart humidifier: http://")); Serial.println(Ethernet.localIP());
  Serial.println(F("Boot PAUSED; sync actual state, then resume AUTO."));
  if (Ethernet.hardwareStatus() == EthernetNoHardware)
    Serial.println(F("Ethernet shield not detected; check hardware."));
#endif
}
void firmwareTick() {
  uint32_t now = millis();
  sensorTick(now); learnTick(now);
  // SoftwareSerial masks interrupts for each byte. Keep IR learning/tests clear
  // of CO2 traffic; failed/old CO2 readings never change the control mode.
  if (learnState == 1 || learnState == 2) {
    co2Serial.stopListening(); co2Waiting = false; co2Used = 0;
  } else {
    co2Serial.listen(); co2Tick(now);
  }
  networkTick(now); // User requests precede automatic decisions; no IR queue.
  controlTick(millis());
}
