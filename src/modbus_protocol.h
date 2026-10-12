#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ModbusProtocol {
enum class Result { Pending, Ok, Timeout, CrcError, InvalidFrame, Exception, Overflow };
uint16_t crc16(const uint8_t* bytes, size_t count);
void appendCrc(uint8_t* frame, size_t payloadLength);
Result validate(const uint8_t* tx, size_t txLength, const uint8_t* rx,
                size_t rxLength, uint8_t& exception);
const char* resultName(Result result);
const char* exceptionName(uint8_t code);
}

