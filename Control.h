#pragma once
#include <stdint.h>
#include "Config.h"
enum Power : uint8_t { POWER_UNKNOWN, OFF, ON };
enum Mode : uint8_t { MANUAL, AUTO, PAUSED };
enum Command : int8_t { NO_COMMAND = -1, POWER_COMMAND, LEVEL_COMMAND, SLEEP_COMMAND };
struct ControlInput {
  uint32_t now, powerAt, levelAt;
  int16_t humidity, trend; // hundredths %RH, trend per minute
  uint8_t target, level, rememberedLevel;
  Power power;
  bool automatic, ready, high, powerCode, levelCode, sleepOff;
};
inline Command decide(const ControlInput &s) {
  if (!s.automatic || !s.ready || s.power == POWER_UNKNOWN || !s.sleepOff) return NO_COMMAND;
  if (s.power == ON && s.high) return s.powerCode ? POWER_COMMAND : NO_COMMAND;
  if (s.power == OFF) {
    if (s.rememberedLevel && s.sleepOff && s.powerCode && s.humidity <= (s.target - 3) * 100 &&
        uint32_t(s.now - s.powerAt) >= Config::MIN_OFF_MS) return POWER_COMMAND;
    return NO_COMMAND;
  }
  if (s.powerCode && s.humidity >= (s.target + 3) * 100 &&
      uint32_t(s.now - s.powerAt) >= Config::MIN_ON_MS) return POWER_COMMAND;
  if (!s.sleepOff) return NO_COMMAND;
  if (uint32_t(s.now - s.levelAt) < Config::MIN_LEVEL_MS) return NO_COMMAND;
  if (s.level == 2 && s.humidity >= (s.target - 2) * 100) {
    if (s.levelCode) return LEVEL_COMMAND;
  }
  if (s.level == 1 && s.levelCode && s.humidity <= (s.target - 5) * 100 && s.trend <= 0)
    return LEVEL_COMMAND;
  return NO_COMMAND;
}
