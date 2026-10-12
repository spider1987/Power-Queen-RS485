#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modbus_client.h"
#include "config.h"
#include "powerqueen.h"
uint32_t clockUs = 0;
using namespace ModbusProtocol;

void frameTests() {
    // Fixed, independently known wire vectors, including CRC byte order.
    uint8_t request[] = {1, 3, 0, 0, 0, 2, 0xC4, 0x0B};
    assert(crc16(request, 6) == 0x0BC4);
    const uint8_t digits[] = {'1','2','3','4','5','6','7','8','9'};
    assert(crc16(digits, 9) == 0x4B37);
    uint8_t ex = 255;
    uint8_t exception[] = {1, 0x83, 2, 0xC0, 0xF1};
    assert(validate(request, 8, exception, 5, ex) == Result::Exception && ex == 2);
    uint8_t response[] = {1, 3, 4, 0, 10, 0, 20, 0, 0};
    appendCrc(response, 7);
    assert(validate(request, 8, response, 9, ex) == Result::Ok);
    response[4] ^= 1;
    assert(validate(request, 8, response, 9, ex) == Result::CrcError);
    response[4] ^= 1;
    response[0] = 2; appendCrc(response, 7);
    assert(validate(request, 8, response, 9, ex) == Result::InvalidFrame);
    response[0] = 1; response[1] = 4; appendCrc(response, 7);
    assert(validate(request, 8, response, 9, ex) == Result::InvalidFrame);
    response[1] = 3; response[2] = 2; appendCrc(response, 7);
    assert(validate(request, 8, response, 9, ex) == Result::InvalidFrame);
    assert(validate(request, 8, response, 4, ex) == Result::InvalidFrame);
    // Local request echo must not become a reading.
    assert(validate(request, 8, request, 8, ex) == Result::InvalidFrame);
    uint8_t write[] = {1, 6, 0, 1, 0, 42, 0, 0}; appendCrc(write, 6);
    assert(validate(write, 8, write, 8, ex) == Result::Ok);
    uint8_t multi[] = {1, 16, 0, 1, 0, 2, 4, 0, 1, 0, 2, 0, 0}; appendCrc(multi, 11);
    uint8_t ack[] = {1, 16, 0, 1, 0, 2, 0, 0}; appendCrc(ack, 6);
    assert(validate(multi, 13, ack, 8, ex) == Result::Ok);
    ack[5] = 3; appendCrc(ack, 6);
    assert(validate(multi, 13, ack, 8, ex) == Result::InvalidFrame);
}
void clientTests() {
    HardwareSerial uart;
    ModbusClient client(uart);
    client.begin(9600, 1);
    clockUs += 5000;
    uint16_t values[] = {1, 2};
    assert(!client.writeSingle(0, 1));
    assert(!client.writeMultiple(0, values, 2));
    assert(uart.sent.empty());
    assert(!client.read(6, 0, 1));
    assert(!client.read(3, 0, 0));
    assert(!client.read(3, 65535, 2));
    assert(!client.configure(9600, 0));
    assert(client.read(3, 0, 2));
    const uint8_t expected[] = {1, 3, 0, 0, 0, 2, 0xC4, 0x0B};
    assert(uart.sent.size() == 8 && !memcmp(uart.sent.data(), expected, 8));
    assert(!client.read(4, 0, 1));
    assert(!client.configure(4800, 2));
    uint8_t response[] = {1, 3, 4, 0, 10, 0, 20, 0, 0}; appendCrc(response, 7);
    for (auto b : response) uart.received.push_back(b);
    client.tick();
    assert(client.busy()); // Must await bus quiet, not just first bytes.
    clockUs += 5000; client.tick();
    assert(client.last().result == Result::Ok && client.stats().successful == 1);
    assert(client.takeCompleted() && !client.takeCompleted());
    assert(!client.read(3, 0, 1)); // Enforce pause after response.
    clockUs += 200000;
    assert(client.read(4, 0, 1));
    clockUs += 500000; client.tick();
    assert(client.last().result == Result::Timeout && client.stats().timeouts == 1);
    assert(client.takeCompleted());
    clockUs += 200000;
    assert(client.read(3, 0, 1));
    for (int i = 0; i < 257; ++i) uart.received.push_back(0);
    client.tick();
    assert(client.last().result == Result::Overflow && client.stats().overflows == 1);
    assert(client.takeCompleted());
    clockUs += 200000;
    assert(client.read(3, 0, 1));
    uart.received.push_back(1); uart.received.push_back(3);
    client.tick(); clockUs += 5000; client.tick();
    assert(client.last().result == Result::InvalidFrame);
    assert(client.takeCompleted());
    clockUs += 200000;
    assert(client.read(3, 0, 1));
    uint8_t exception[] = {1, 0x83, 2, 0xC0, 0xF1};
    for (auto b : exception) uart.received.push_back(b);
    client.tick(); clockUs += 5000; client.tick();
    assert(client.last().result == Result::Exception && client.stats().exceptions == 1);
    assert(client.takeCompleted());
    assert(client.stats().requests == 5);
}
int main() {
    frameTests(); clientTests();
    PowerQueenMonitor monitor;
    monitor.begin();
    assert(isnan(monitor.data.batteryVoltage));
    assert(!monitor.data.chargeStateValid && !monitor.communicationOnline());
    ModbusTransaction t;
    t.result = Result::Exception; t.completedAt = millis();
    monitor.observe(t);
    assert(monitor.communicationOnline());
    assert(!monitor.data.online && isnan(monitor.data.pvPower));
    for (int i = 0; i < 370; ++i) { clockUs += 10000000; monitor.tick(); }
    assert(monitor.historyCount() == HISTORY_CAPACITY);
    assert(!monitor.communicationOnline());
    assert(isnan(monitor.sample(0).pvPower));
    assert(uint32_t(monitor.sample(360).timestamp - monitor.sample(0).timestamp) == 3600000UL);
    puts("PASS: CRC vectors, frame validation, write interlocks, UART state machine, pacing, timeout, overflow, exceptions");
    puts("PASS: unknown data, communication validity, 60-minute history retention and wrap");
}
