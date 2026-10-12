#pragma once
// Minimal fake UART and clock for host-side protocol/client tests only.
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <deque>
constexpr int OUTPUT = 1, LOW = 0, HIGH = 1, SERIAL_8N1 = 0;
extern uint32_t clockUs;
inline uint32_t micros() { return clockUs; }
inline uint32_t millis() { return clockUs / 1000; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
class HardwareSerial {
public:
    std::vector<uint8_t> sent;
    std::deque<uint8_t> received;
    void begin(uint32_t, int, int, int) {}
    void end() {}
    size_t write(const uint8_t* data, size_t n) {
        sent.insert(sent.end(), data, data + n); return n;
    }
    void flush() {}
    int available() { return received.size(); }
    int read() { const auto b = received.front(); received.pop_front(); return b; }
};

