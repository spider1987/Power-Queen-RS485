#include "powerqueen.h"
#include "registers.h"
#include <string.h>
void PowerQueenMonitor::begin() { lastSample_ = millis(); }
void PowerQueenMonitor::observe(const ModbusTransaction& t) {
    if (t.result != ModbusProtocol::Result::Ok && t.result != ModbusProtocol::Result::Exception) return;
    hasResponse_ = true; lastResponse_ = t.completedAt;
    if (t.result != ModbusProtocol::Result::Ok) return;
    bool mapped = false;
    for (size_t i = 0; i < POWERQUEEN_REGISTER_COUNT; ++i) {
        const auto& r = POWERQUEEN_REGISTERS[i];
        if (r.functionCode != t.function || r.address < t.address ||
            uint32_t(r.address) >= uint32_t(t.address) + t.count) continue;
        const size_t offset = 3 + (r.address - t.address) * 2;
        const uint16_t raw = uint16_t(t.rx[offset]) << 8 | t.rx[offset + 1];
        const float value = raw * r.scale;
        // Initial decoder supports single unsigned 16-bit values only.
        // Signed/multiword fields require an explicitly verified decoder extension.
        if (!r.name || !isfinite(value)) continue;
        struct Binding { const char* name; float* target; };
        Binding bindings[] = {
            {"batteryVoltage", &data.batteryVoltage}, {"batteryCurrent", &data.batteryCurrent},
            {"batteryPower", &data.batteryPower}, {"pvVoltage", &data.pvVoltage},
            {"pvCurrent", &data.pvCurrent}, {"pvPower", &data.pvPower},
            {"loadVoltage", &data.loadVoltage}, {"loadCurrent", &data.loadCurrent},
            {"loadPower", &data.loadPower}, {"controllerTemperature", &data.controllerTemperature},
            {"batteryTemperature", &data.batteryTemperature}, {"batterySoc", &data.batterySoc},
            {"dailyYieldWh", &data.dailyYieldWh}, {"totalYieldWh", &data.totalYieldWh}
        };
        for (const auto& b : bindings) if (!strcmp(r.name, b.name)) { *b.target = value; mapped = true; }
        if (!strcmp(r.name, "chargeState")) { data.chargeState = raw; data.chargeStateValid = true; mapped = true; }
        if (!strcmp(r.name, "faultCode")) { data.faultCode = raw; data.faultCodeValid = true; mapped = true; }
    }
    if (mapped) { data.lastUpdate = t.completedAt; data.online = true; }
}
bool PowerQueenMonitor::communicationOnline() const {
    return hasResponse_ && uint32_t(millis() - lastResponse_) < DATA_OFFLINE_MS;
}
const char* PowerQueenMonitor::communicationState() const {
    if (!communicationOnline()) return "offline";
    return uint32_t(millis() - lastResponse_) >= DATA_STALE_MS ? "stale" : "online";
}
void PowerQueenMonitor::tick() {
    if (data.online && uint32_t(millis() - data.lastUpdate) >= DATA_OFFLINE_MS) data.online = false;
    if (uint32_t(millis() - lastSample_) < HISTORY_INTERVAL_MS) return;
    lastSample_ = millis();
    const bool fresh = data.online && uint32_t(millis() - data.lastUpdate) < DATA_STALE_MS;
    history_[head_] = {lastSample_, fresh ? data.pvPower : NAN,
        fresh ? data.batteryVoltage : NAN, fresh ? data.batteryCurrent : NAN,
        fresh ? data.controllerTemperature : NAN};
    head_ = (head_ + 1) % HISTORY_CAPACITY;
    if (count_ < HISTORY_CAPACITY) ++count_;
}
const HistorySample& PowerQueenMonitor::sample(uint16_t index) const {
    return history_[(head_ + HISTORY_CAPACITY - count_ + index) % HISTORY_CAPACITY];
}

