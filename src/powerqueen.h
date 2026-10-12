#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"
#include "modbus_client.h"

struct PowerQueenData {
    float batteryVoltage = NAN, batteryCurrent = NAN, batteryPower = NAN;
    float pvVoltage = NAN, pvCurrent = NAN, pvPower = NAN;
    float loadVoltage = NAN, loadCurrent = NAN, loadPower = NAN;
    float controllerTemperature = NAN, batteryTemperature = NAN;
    float batterySoc = NAN, dailyYieldWh = NAN, totalYieldWh = NAN;
    uint16_t chargeState = 0, faultCode = 0;
    // Integer states cannot represent NAN. Never display without validity flags.
    bool chargeStateValid = false, faultCodeValid = false;
    bool online = false;
    uint32_t lastUpdate = 0;
};

struct HistorySample {
    uint32_t timestamp;
    float pvPower, batteryVoltage, batteryCurrent, temperature;
};
class PowerQueenMonitor {
public:
    PowerQueenData data;
    void begin();
    void observe(const ModbusTransaction& transaction);
    void tick();
    bool communicationOnline() const;
    const char* communicationState() const;
    bool hasResponse() const { return hasResponse_; }
    uint32_t lastResponse() const { return lastResponse_; }
    uint16_t historyCount() const { return count_; }
    const HistorySample& sample(uint16_t index) const;
private:
    HistorySample history_[HISTORY_CAPACITY];
    uint16_t head_ = 0, count_ = 0;
    uint32_t lastSample_ = 0, lastResponse_ = 0;
    bool hasResponse_ = false;
};
extern PowerQueenMonitor powerqueen;

