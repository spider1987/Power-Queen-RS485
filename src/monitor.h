#pragma once
#include "modbus_client.h"
extern ModbusClient modbus;
bool startScan(uint16_t start, uint16_t end, uint8_t function, uint8_t count);
bool startDiagnosis();
void stopMonitorJob();
const char* monitorJobName();
