#pragma once
#include <stdint.h>
// Extra decoders exceed Uno flash with the full UI. RAW fallback stays enabled.
#define HUMIDIFIER_EXTENDED_IR 0
namespace Config {
constexpr uint8_t IR_RX = 2, IR_TX = 3, SD_CS = 4, ETH_CS = 10;
constexpr uint8_t SHT_ADDRESS = 0x44;
// Set an unused address outside your router's DHCP pool. No DHCP blocking.
constexpr uint8_t IP[4] = {192, 168, 0, 50};
constexpr uint8_t GATEWAY[4] = {192, 168, 0, 1};
constexpr uint8_t SUBNET[4] = {255, 255, 255, 0};
constexpr uint8_t MAC[6] = {0x02, 0x48, 0x55, 0x4D, 0x00, 0x01};
constexpr uint32_t MIN_ON_MS = 300000UL, MIN_OFF_MS = 900000UL;
constexpr uint32_t MIN_LEVEL_MS = 300000UL, IR_GAP_MS = 1500UL;
constexpr uint32_t SENSOR_STALE_MS = 30000UL;
constexpr uint8_t RAW_MAX = 100, EVENT_COUNT = 6;
}
