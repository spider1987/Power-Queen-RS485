#pragma once
#include <Arduino.h>

// Example pins only: verify against your exact ESP32-S3 board before wiring.
#define RS485_UART_NUMBER 1
#define RS485_RX_PIN 18
#define RS485_TX_PIN 17
// -1: automatic-direction adapter. Otherwise active HIGH DE, tied to /RE.
#define RS485_DIRECTION_PIN -1
#define MODBUS_BAUD 9600UL
#define MODBUS_SLAVE_ID 1
#define MODBUS_WRITE_ENABLED false
#define MODBUS_TIMEOUT_MS 500UL
#define MODBUS_REQUEST_PAUSE_MS 200UL
#define SCAN_MAX_REGISTERS 4
// No bus traffic at boot. Start explicitly using the serial commands.
#define AUTO_SCAN_ENABLED false
#define FIRMWARE_VERSION "0.1.0"
// Optional local credentials, ignored by Git.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#define WIFI_FALLBACK_MS 30000UL
#define WIFI_AP_SSID "PowerQueen-Setup"
#define WIFI_AP_PASSWORD "" // Optional WPA2 password, >=8 characters.
#define MQTT_ENABLED false
#define MQTT_HOST ""
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""
#define MQTT_PREFIX "powerqueen"
#define DATA_STALE_MS 15000UL
#define DATA_OFFLINE_MS 60000UL
#define HISTORY_INTERVAL_MS 10000UL
#define HISTORY_CAPACITY 361
static_assert(SCAN_MAX_REGISTERS >= 1 && SCAN_MAX_REGISTERS <= 4, "Scan limit 1..4");
static_assert(MODBUS_SLAVE_ID >= 1 && MODBUS_SLAVE_ID <= 247, "No broadcast");

