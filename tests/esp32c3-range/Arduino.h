#pragma once
// Host-only Arduino/serial shim. Never included by the ESP32 sketch build.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int OUTPUT = 1;
constexpr int INPUT_PULLUP = 2;
constexpr int SERIAL_8N1 = 0;
inline uint32_t fakeMillis = 0;
inline int gpio[64] = {};
inline int rgbRed[64] = {}, rgbGreen[64] = {}, rgbBlue[64] = {};
inline void rgbLedWrite(int pin, int red, int green, int blue) {
  rgbRed[pin] = red; rgbGreen[pin] = green; rgbBlue[pin] = blue;
}
inline uint32_t millis() { return fakeMillis; }
inline void delay(uint32_t duration) { fakeMillis += duration; }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int level) { gpio[pin] = level; }

class HardwareSerial {
 public:
  explicit HardwareSerial(int) {}
  std::deque<uint8_t> incoming;
  std::function<std::vector<uint8_t>(const std::vector<uint8_t> &)> responder;
  std::string log;
  int rxPin = -1;
  int txPin = -1;
  void begin(uint32_t, int = 0, int rx = -1, int tx = -1) { rxPin = rx; txPin = tx; }
  int available() { return static_cast<int>(incoming.size()); }
  int read() {
    if (incoming.empty()) return -1;
    const uint8_t value = incoming.front();
    incoming.pop_front();
    return value;
  }
  size_t write(const uint8_t *data, size_t length) {
    if (responder) {
      const auto reply = responder(std::vector<uint8_t>(data, data + length));
      incoming.insert(incoming.end(), reply.begin(), reply.end());
    }
    return length;
  }
  void flush() {}
  void print(const char *value) { log += value; }
  void println(const char *value) { log += value; log += '\n'; }
  template <typename... Args> void printf(const char *format, Args... args) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    log += buffer;
  }
};
inline HardwareSerial Serial(0);
