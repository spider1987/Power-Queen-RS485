#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "modbus_client.h"
#include "monitor.h"
#include "powerqueen.h"
#include "webserver_api.h"
#include "mqtt.h"

HardwareSerial rs485(RS485_UART_NUMBER);
ModbusClient modbus(rs485);
enum class Job { Idle, Scan, Diagnose };
Job job = Job::Idle;
uint32_t nextAddress = 0;
uint16_t scanEnd = 0;
uint8_t scanFunction = 3, scanCount = 1;
bool stopRequested = false;
const uint32_t diagnosticBauds[] = {2400, 4800, 9600, 19200};
const uint8_t diagnosticSlaves[] = {1, 2, 10, 16, 247};
// Probe addresses are hypotheses, NOT a register map or decoded measurements.
const uint16_t diagnosticAddresses[] = {0x0000, 0x0100};
size_t diagnosticIndex = 0;
uint32_t savedBaud = MODBUS_BAUD;
uint8_t savedSlave = MODBUS_SLAVE_ID;
bool diagnosticConfigured = false;
char command[96];
size_t commandLength = 0;
bool commandOverflow = false;
PowerQueenMonitor powerqueen;

bool startScan(uint16_t start, uint16_t end, uint8_t function, uint8_t count) {
    if (job != Job::Idle || modbus.busy() || start > end ||
        (function != 3 && function != 4) || count < 1 || count > SCAN_MAX_REGISTERS) return false;
    nextAddress = start; scanEnd = end; scanFunction = function; scanCount = count;
    stopRequested = false; job = Job::Scan; return true;
}
bool startDiagnosis() {
    if (job != Job::Idle || modbus.busy()) return false;
    savedBaud = modbus.baud(); savedSlave = modbus.slave();
    diagnosticIndex = 0; diagnosticConfigured = false;
    stopRequested = false; job = Job::Diagnose; return true;
}
void stopMonitorJob() { stopRequested = true; }
const char* monitorJobName() {
    return job == Job::Scan ? "scan" : job == Job::Diagnose ? "diagnose" : "idle";
}

void hexFrame(const char* label, const uint8_t* frame, size_t count) {
    Serial.print(label);
    for (size_t i = 0; i < count; ++i) Serial.printf("%02X%s", frame[i], i + 1 == count ? "" : " ");
    if (!count) Serial.print("(empty)");
    Serial.println();
}
void printTransaction() {
    const auto& t = modbus.last();
    Serial.printf("[RTU] Baud=%lu Slave=%u FC=%02X Addr=0x%04X Count=%u Result=%s\n",
        static_cast<unsigned long>(t.baud), t.slave, t.function, t.address, t.count,
        ModbusProtocol::resultName(t.result));
    hexFrame("TX: ", t.tx, t.txLength);
    hexFrame("RX: ", t.rx, t.rxLength);
    if (t.result == ModbusProtocol::Result::Exception)
        Serial.printf("Exception %02X: %s (CRC OK)\n", t.exception,
                      ModbusProtocol::exceptionName(t.exception));
    if (t.result == ModbusProtocol::Result::Ok) {
        Serial.println("CRC OK");
        for (uint16_t i = 0; i < t.count; ++i)
            Serial.printf("[SCAN] Addr 0x%04X = %u\n", t.address + i,
                          uint16_t(t.rx[3 + i * 2]) << 8 | t.rx[4 + i * 2]);
    } else {
        for (uint16_t i = 0; i < t.count; ++i)
            Serial.printf("[SCAN] Addr 0x%04X = %s\n", t.address + i,
                          ModbusProtocol::resultName(t.result));
    }
}
void finishJob() {
    if (job == Job::Diagnose) modbus.configure(savedBaud, savedSlave);
    job = Job::Idle; stopRequested = false;
    Serial.println("Job finished. Bus idle; no automatic polling.");
}
void help() {
    Serial.println("Commands (newline, addresses decimal or 0xHEX):");
    Serial.println(" scan START END FC [COUNT]  e.g. scan 0x0100 0x01ff 3 1");
    Serial.println(" read ADDRESS FC           e.g. read 0x0100 4");
    Serial.println(" diagnose                  80 read-only probes; restores settings");
    Serial.println(" stop                      stops after current bounded request");
    Serial.println(" baud VALUE                2400/4800/9600/19200 (idle only)");
    Serial.println(" slave ID                  1..247 (idle only)");
    Serial.println(" stats | help");
    Serial.println("FC must be 3 or 4. Writes have no serial command and are disabled.");
}
bool parseNumber(const char* s, uint32_t& value) {
    if (!s || !*s || *s == '-' || *s == '+') return false;
    char* end = nullptr;
    value = strtoul(s, &end, (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10);
    return end && !*end && strlen(s) <= 10;
}
void executeCommand() {
    char* args[6] = {};
    size_t n = 0;
    char* state = nullptr;
    for (char* p = strtok_r(command, " \t", &state); p; p = strtok_r(nullptr, " \t", &state)) {
        if (n == 6) { Serial.println("Too many arguments"); return; }
        args[n++] = p;
    }
    if (!n) return;
    if (!strcmp(args[0], "help") && n == 1) { help(); return; }
    if (!strcmp(args[0], "stats") && n == 1) {
        const auto& s = modbus.stats();
        Serial.printf("Baud=%lu Slave=%u UART initialized (not proof of device connection)\n",
                      static_cast<unsigned long>(modbus.baud()), modbus.slave());
        Serial.printf("requests=%lu ok=%lu timeout=%lu crc=%lu exception=%lu invalid=%lu overflow=%lu\n",
            static_cast<unsigned long>(s.requests), static_cast<unsigned long>(s.successful),
            static_cast<unsigned long>(s.timeouts), static_cast<unsigned long>(s.crcErrors),
            static_cast<unsigned long>(s.exceptions), static_cast<unsigned long>(s.invalidFrames),
            static_cast<unsigned long>(s.overflows));
        return;
    }
    if (!strcmp(args[0], "stop") && n == 1) { stopRequested = true; return; }
    if (job != Job::Idle || modbus.busy()) { Serial.println("Busy. Use stop first."); return; }
    uint32_t v[4] = {};
    for (size_t i = 1; i < n; ++i) {
        if (i > 4 || !parseNumber(args[i], v[i - 1])) { Serial.println("Invalid number"); return; }
    }
    if (!strcmp(args[0], "scan") && (n == 4 || n == 5) && v[0] <= v[1] &&
        v[1] <= 65535 && (v[2] == 3 || v[2] == 4) &&
        (n == 4 || (v[3] >= 1 && v[3] <= SCAN_MAX_REGISTERS))) {
        nextAddress = v[0]; scanEnd = v[1]; scanFunction = v[2];
        scanCount = n == 5 ? v[3] : 1; job = Job::Scan; stopRequested = false;
    } else if (!strcmp(args[0], "read") && n == 3 && v[0] <= 65535 && (v[1] == 3 || v[1] == 4)) {
        nextAddress = scanEnd = v[0]; scanFunction = v[1]; scanCount = 1;
        job = Job::Scan; stopRequested = false;
    } else if (!strcmp(args[0], "diagnose") && n == 1) {
        savedBaud = modbus.baud(); savedSlave = modbus.slave();
        diagnosticIndex = 0; diagnosticConfigured = false;
        job = Job::Diagnose; stopRequested = false;
        Serial.println("Diagnosis started. Valid exceptions prove RTU response, not readable registers.");
    } else if (!strcmp(args[0], "baud") && n == 2 &&
        (v[0] == 2400 || v[0] == 4800 || v[0] == 9600 || v[0] == 19200)) {
        modbus.configure(v[0], modbus.slave());
    } else if (!strcmp(args[0], "slave") && n == 2 && v[0] >= 1 && v[0] <= 247) {
        modbus.configure(modbus.baud(), v[0]);
    } else Serial.println("Invalid command. Use help.");
}
void serialCommands() {
    for (size_t i = 0; i < 128 && Serial.available(); ++i) {
        const char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (commandOverflow) Serial.println("Command too long; discarded");
            else { command[commandLength] = 0; executeCommand(); }
            commandLength = 0; commandOverflow = false;
        } else if (commandLength < sizeof(command) - 1 && !commandOverflow) {
            command[commandLength++] = c;
        } else commandOverflow = true;
    }
}
void serviceJob() {
    if (modbus.takeCompleted()) {
        printTransaction();
        powerqueen.observe(modbus.last());
        if (job == Job::Diagnose) {
            const auto& t = modbus.last();
            if (t.result == ModbusProtocol::Result::Ok || t.result == ModbusProtocol::Result::Exception)
                Serial.printf("Possible device found: Baud=%lu Slave=%u FC=%02X Register=0x%04X (%s)\n",
                    static_cast<unsigned long>(t.baud), t.slave, t.function, t.address,
                    t.result == ModbusProtocol::Result::Ok ? "readable" : "valid exception only");
            ++diagnosticIndex; diagnosticConfigured = false;
        }
    }
    if (job == Job::Idle || modbus.busy()) return;
    if (stopRequested) { finishJob(); return; }
    if (job == Job::Scan) {
        if (nextAddress > scanEnd) { finishJob(); return; }
        const uint16_t count = min(uint32_t(scanCount), uint32_t(scanEnd) - nextAddress + 1);
        if (modbus.read(scanFunction, nextAddress, count)) nextAddress += count;
    } else {
        if (diagnosticIndex >= 80) { finishJob(); return; }
        const size_t baudIndex = diagnosticIndex / 20;
        const size_t slaveIndex = (diagnosticIndex / 4) % 5;
        const uint8_t function = 3 + (diagnosticIndex / 2) % 2;
        if (!diagnosticConfigured) {
            modbus.configure(diagnosticBauds[baudIndex], diagnosticSlaves[slaveIndex]);
            diagnosticConfigured = true;
        }
        modbus.read(function, diagnosticAddresses[diagnosticIndex % 2], 1);
    }
}
void setup() {
    Serial.begin(115200);
    // Bounded wait for USB monitor; firmware also runs without an attached PC.
    const uint32_t started = millis();
    while (!Serial && uint32_t(millis() - started) < 1500) delay(10);
    Serial.println("\n========================================");
    Serial.println("Power Queen M2430N Monitor\nESP32-S3\nFirmware " FIRMWARE_VERSION);
    Serial.println("========================================");
    modbus.begin(MODBUS_BAUD, MODBUS_SLAVE_ID);
    Serial.printf("RS485 UART%d RX: GPIO %d TX: GPIO %d\nBaud: %lu Slave ID: %u\n",
        RS485_UART_NUMBER, RS485_RX_PIN, RS485_TX_PIN,
        static_cast<unsigned long>(modbus.baud()), modbus.slave());
    Serial.printf("Writes: %s\n", MODBUS_WRITE_ENABLED ? "ENABLED" : "BLOCKED");
    Serial.println("Modbus: ready. No confirmed register mapping.");
    help();
    powerqueen.begin();
    webBegin();
    mqttBegin();
    if (AUTO_SCAN_ENABLED) { job = Job::Scan; nextAddress = 0; scanEnd = 0x00FF; }
}
void loop() {
    modbus.tick();
    serviceJob();
    serialCommands();
    powerqueen.tick();
    webTick();
    mqttTick();
    delay(1);
}

