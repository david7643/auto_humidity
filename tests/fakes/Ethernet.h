#pragma once
#include "Arduino.h"
struct IPAddress { explicit IPAddress(const uint8_t *) {} operator int() const { return 0; } };
class EthernetClient {
 public:
  bool live=false;
  std::string request, response;
  explicit operator bool() const { return live; }
  void stop() { live=false; }
  void setConnectionTimeout(int) {}
  int availableForWrite() { return 2048; }
  size_t write(const uint8_t *p, size_t n) { response.append(reinterpret_cast<const char *>(p),n); return n; }
  bool connected() { return live; }
  int available() { return int(request.size()); }
  int read() { int b=request.front(); request.erase(request.begin()); return b; }
};
class EthernetServer {
 public:
  explicit EthernetServer(int) {}
  void begin() {}
  EthernetClient accept() { return EthernetClient(); }
};
constexpr int EthernetNoHardware=0;
struct EthernetFake {
  void init(int) {}
  void begin(uint8_t*, IPAddress, IPAddress, IPAddress, IPAddress) {}
  void setRetransmissionTimeout(int) {}
  void setRetransmissionCount(int) {}
  int localIP() { return 0; }
  int hardwareStatus() { return 1; }
};
inline EthernetFake Ethernet;
