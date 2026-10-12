#include "modbus_protocol.h"
#include <string.h>

namespace ModbusProtocol {
uint16_t crc16(const uint8_t* bytes, size_t count) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}
void appendCrc(uint8_t* frame, size_t n) {
    const auto crc = crc16(frame, n);
    frame[n] = crc & 0xFF;
    frame[n + 1] = crc >> 8;
}
Result validate(const uint8_t* tx, size_t tn, const uint8_t* rx, size_t rn, uint8_t& ex) {
    ex = 0;
    if (tn < 8 || rn < 5) return Result::InvalidFrame;
    if (crc16(rx, rn - 2) != (uint16_t(rx[rn - 1]) << 8 | rx[rn - 2]))
        return Result::CrcError;
    if (rx[0] != tx[0]) return Result::InvalidFrame;
    if (rx[1] == (tx[1] | 0x80)) {
        if (rn != 5) return Result::InvalidFrame;
        ex = rx[2];
        return Result::Exception;
    }
    if (rx[1] != tx[1]) return Result::InvalidFrame;
    const uint16_t count = uint16_t(tx[4]) << 8 | tx[5];
    if (tx[1] == 3 || tx[1] == 4) {
        if (!count || count > 125 || rn != size_t(count) * 2 + 5 || rx[2] != count * 2)
            return Result::InvalidFrame;
    } else if (tx[1] == 6 || tx[1] == 16) {
        if (rn != 8 || memcmp(tx, rx, 6) != 0) return Result::InvalidFrame;
    } else return Result::InvalidFrame;
    return Result::Ok;
}
const char* resultName(Result r) {
    switch (r) {
        case Result::Pending: return "pending";
        case Result::Ok: return "ok";
        case Result::Timeout: return "timeout";
        case Result::CrcError: return "CRC error";
        case Result::InvalidFrame: return "invalid frame";
        case Result::Exception: return "exception";
        case Result::Overflow: return "RX overflow";
    }
    return "unknown";
}
const char* exceptionName(uint8_t code) {
    switch (code) {
        case 1: return "Illegal Function";
        case 2: return "Illegal Data Address";
        case 3: return "Illegal Data Value";
        case 4: return "Slave Device Failure";
        case 5: return "Acknowledge";
        case 6: return "Slave Device Busy";
        case 8: return "Memory Parity Error";
        case 10: return "Gateway Path Unavailable";
        case 11: return "Gateway Target Failed to Respond";
        default: return "Unknown Exception";
    }
}
}

