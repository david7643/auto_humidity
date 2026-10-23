#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "Config.h"
// Bump when changing IRremote protocol IDs or record format.
constexpr uint8_t SCHEMA = 1;
struct __attribute__((packed)) Settings {
  // Reserved bytes retain the old layout, preserving learned IR EEPROM offsets.
  uint8_t schema, target, autoWanted, reserved1, reserved2;
  uint16_t sequence, crc;
};
struct __attribute__((packed)) IrRecord {
  uint8_t schema, protocol, bits, length, repeats, khz;
  uint16_t address, command, raw[Config::RAW_MAX], crc;
};
constexpr int IR_BASE = 2 * sizeof(Settings);
static_assert(IR_BASE + 3 * sizeof(IrRecord) <= 1024, "EEPROM capacity exceeded");
inline uint16_t checksum(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  while (length--) {
    crc ^= uint16_t(*data++) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}
template <class T> bool readRecord(int address, T &record) {
  EEPROM.get(address, record);
  return record.schema == SCHEMA && record.crc == checksum(
    reinterpret_cast<const uint8_t *>(&record), sizeof(T) - 2);
}
template <class T> void writeRecord(int address, T &record) {
  record.schema = SCHEMA;
  record.crc = checksum(reinterpret_cast<const uint8_t *>(&record), sizeof(T) - 2);
  EEPROM.update(address, 0);
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
  for (size_t i = 1; i < sizeof(T); ++i) EEPROM.update(address + i, bytes[i]);
  EEPROM.update(address, SCHEMA); // commit last
}
