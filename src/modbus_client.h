#pragma once
#include <Arduino.h>
#include "modbus_protocol.h"

struct ModbusStats {
    uint32_t requests = 0, successful = 0, timeouts = 0;
    uint32_t crcErrors = 0, exceptions = 0, invalidFrames = 0, overflows = 0;
};
struct ModbusTransaction {
    uint8_t tx[256] = {}, rx[256] = {};
    size_t txLength = 0, rxLength = 0;
    uint32_t startedAt = 0, completedAt = 0;
    uint32_t baud = 0;
    uint8_t slave = 0, function = 0, exception = 0;
    uint16_t address = 0, count = 0;
    ModbusProtocol::Result result = ModbusProtocol::Result::Pending;
};
class ModbusClient {
public:
    explicit ModbusClient(HardwareSerial& serial) : serial_(serial) {}
    void begin(uint32_t baud, uint8_t slave);
    bool configure(uint32_t baud, uint8_t slave);
    bool read(uint8_t function, uint16_t address, uint16_t count);
    bool writeSingle(uint16_t address, uint16_t value);
    bool writeMultiple(uint16_t address, const uint16_t* values, uint16_t count);
    void tick();
    bool busy() const { return busy_; }
    bool takeCompleted();
    const ModbusTransaction& last() const { return transaction_; }
    const ModbusStats& stats() const { return stats_; }
    uint32_t baud() const { return baud_; }
    uint8_t slave() const { return slave_; }
private:
    bool send(size_t payloadLength);
    bool ready() const;
    void header(uint8_t function, uint16_t address, uint16_t value);
    void finish(ModbusProtocol::Result result);
    HardwareSerial& serial_;
    ModbusTransaction transaction_;
    ModbusStats stats_;
    uint32_t baud_ = 9600, lastByteUs_ = 0, quietUs_ = 0, finishedAt_ = 0;
    uint8_t slave_ = 1;
    bool busy_ = false, completed_ = false, haveFinished_ = false;
};

