#include "modbus_client.h"
#include "config.h"

void ModbusClient::begin(uint32_t baud, uint8_t slave) {
    if (RS485_DIRECTION_PIN >= 0) {
        pinMode(RS485_DIRECTION_PIN, OUTPUT);
        digitalWrite(RS485_DIRECTION_PIN, LOW);
    }
    configure(baud, slave);
}
bool ModbusClient::configure(uint32_t baud, uint8_t slave) {
    if (busy_ || !baud || slave < 1 || slave > 247) return false;
    serial_.end();
    baud_ = baud; slave_ = slave;
    serial_.begin(baud_, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    // t3.5: 3.5 * 10 bits for 8N1; fixed 1.75ms above 19200 baud.
    quietUs_ = baud_ > 19200 ? 1750 : (35000000UL + baud_ - 1) / baud_;
    lastByteUs_ = micros();
    return true;
}
bool ModbusClient::ready() const {
    return !busy_ && !completed_ &&
        (!haveFinished_ || uint32_t(millis() - finishedAt_) >= MODBUS_REQUEST_PAUSE_MS) &&
        uint32_t(micros() - lastByteUs_) >= quietUs_;
}
void ModbusClient::header(uint8_t f, uint16_t a, uint16_t v) {
    transaction_ = ModbusTransaction{};
    transaction_.baud = baud_; transaction_.slave = slave_;
    transaction_.function = f; transaction_.address = a;
    auto* tx = transaction_.tx;
    tx[0] = slave_; tx[1] = f; tx[2] = a >> 8; tx[3] = a;
    tx[4] = v >> 8; tx[5] = v;
}
bool ModbusClient::read(uint8_t f, uint16_t a, uint16_t n) {
    if (!ready() || (f != 3 && f != 4) || !n || n > 125 || uint32_t(a) + n > 65536UL)
        return false;
    header(f, a, n); transaction_.count = n;
    return send(6);
}
bool ModbusClient::writeSingle(uint16_t a, uint16_t v) {
    if (!MODBUS_WRITE_ENABLED || !ready()) return false;
    header(6, a, v); transaction_.count = 1;
    return send(6);
}
bool ModbusClient::writeMultiple(uint16_t a, const uint16_t* values, uint16_t n) {
    if (!MODBUS_WRITE_ENABLED || !ready() || !values || !n || n > 123 ||
        uint32_t(a) + n > 65536UL) return false;
    header(16, a, n); transaction_.count = n;
    transaction_.tx[6] = n * 2;
    for (uint16_t i = 0; i < n; ++i) {
        transaction_.tx[7 + i * 2] = values[i] >> 8;
        transaction_.tx[8 + i * 2] = values[i];
    }
    return send(7 + n * 2);
}
bool ModbusClient::send(size_t n) {
    ModbusProtocol::appendCrc(transaction_.tx, n);
    transaction_.txLength = n + 2;
    if (RS485_DIRECTION_PIN >= 0) digitalWrite(RS485_DIRECTION_PIN, HIGH);
    serial_.write(transaction_.tx, transaction_.txLength);
    // Bounded UART drain (maximum 256 bytes), yields inside ESP32 driver.
    serial_.flush();
    if (RS485_DIRECTION_PIN >= 0) digitalWrite(RS485_DIRECTION_PIN, LOW);
    transaction_.startedAt = millis(); lastByteUs_ = micros();
    busy_ = true; ++stats_.requests;
    return true;
}
void ModbusClient::tick() {
    // Bound work even on a continuously noisy wire.
    for (size_t i = 0; i < 512 && serial_.available(); ++i) {
        const uint8_t b = serial_.read();
        lastByteUs_ = micros();
        if (!busy_) continue; // Drain unsolicited/late bytes before next request.
        if (transaction_.rxLength == sizeof(transaction_.rx)) {
            finish(ModbusProtocol::Result::Overflow);
            continue;
        }
        transaction_.rx[transaction_.rxLength++] = b;
    }
    if (!busy_) return;
    if (transaction_.rxLength && uint32_t(micros() - lastByteUs_) >= quietUs_) {
        finish(ModbusProtocol::validate(transaction_.tx, transaction_.txLength,
            transaction_.rx, transaction_.rxLength, transaction_.exception));
    } else if (uint32_t(millis() - transaction_.startedAt) >= MODBUS_TIMEOUT_MS) {
        finish(transaction_.rxLength ? ModbusProtocol::Result::InvalidFrame :
                                     ModbusProtocol::Result::Timeout);
    }
}
void ModbusClient::finish(ModbusProtocol::Result result) {
    transaction_.result = result; transaction_.completedAt = millis();
    finishedAt_ = millis(); haveFinished_ = true; busy_ = false; completed_ = true;
    using R = ModbusProtocol::Result;
    switch (result) {
        case R::Ok: ++stats_.successful; break;
        case R::Timeout: ++stats_.timeouts; break;
        case R::CrcError: ++stats_.crcErrors; break;
        case R::Exception: ++stats_.exceptions; break;
        case R::Overflow: ++stats_.overflows; break;
        default: ++stats_.invalidFrames; break;
    }
}
bool ModbusClient::takeCompleted() {
    const bool value = completed_; completed_ = false; return value;
}

