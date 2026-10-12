#include "webserver_api.h"
#include "config.h"
#include "monitor.h"
#include "powerqueen.h"
#include "registers.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <esp_system.h>

static WebServer server(80);
static bool fsReady = false, apStarted = false, hadWifi = false;
static uint32_t wifiStarted = 0;
static String scanToken;
static String number(float value) { return isfinite(value) ? String(value, 3) : String("null"); }
static String hex(const uint8_t* bytes, size_t n) {
    String result; result.reserve(n * 3);
    char b[4];
    for (size_t i = 0; i < n; ++i) { snprintf(b, sizeof(b), "%02X", bytes[i]); if (i) result += ' '; result += b; }
    return result;
}
String statsJson() {
    const auto& s = modbus.stats();
    return String("{\"requests\":") + s.requests + ",\"successful\":" + s.successful +
        ",\"timeouts\":" + s.timeouts + ",\"crcErrors\":" + s.crcErrors +
        ",\"exceptions\":" + s.exceptions + ",\"invalidFrames\":" + s.invalidFrames +
        ",\"overflows\":" + s.overflows + "}";
}
String rawJson() {
    const auto& t = modbus.last();
    const auto r = t.result;
    const char* crc = !t.txLength ? "not_checked" :
        (r == ModbusProtocol::Result::Ok || r == ModbusProtocol::Result::Exception) ? "ok" :
        r == ModbusProtocol::Result::CrcError ? "error" : "not_verified";
    return String("{\"uartInitialized\":true,\"baud\":") + modbus.baud() +
        ",\"slave\":" + modbus.slave() + ",\"job\":\"" + monitorJobName() +
        "\",\"busy\":" + (modbus.busy() ? "true" : "false") +
        ",\"lastQuery\":" + (t.txLength ? String(t.startedAt) : String("null")) +
        ",\"queryBaud\":" + t.baud + ",\"querySlave\":" + t.slave +
        ",\"functionCode\":" + t.function + ",\"address\":" + t.address +
        ",\"count\":" + t.count + ",\"result\":\"" + (t.txLength ? ModbusProtocol::resultName(r) : "none") +
        "\",\"crc\":\"" + crc + "\",\"exception\":" + t.exception +
        ",\"exceptionName\":\"" + (r == ModbusProtocol::Result::Exception ? ModbusProtocol::exceptionName(t.exception) : "") +
        "\",\"tx\":\"" + hex(t.tx, t.txLength) + "\",\"rx\":\"" + hex(t.rx, t.rxLength) + "\"}";
}
String statusJson() {
    const auto& d = powerqueen.data;
    const char* dataState = !d.online ? "offline" :
        uint32_t(millis() - d.lastUpdate) >= DATA_STALE_MS ? "stale" : "online";
    return String("{\"online\":") + (powerqueen.communicationOnline() ? "true" : "false") +
        ",\"communicationState\":\"" + powerqueen.communicationState() +
        "\",\"dataOnline\":" + (d.online ? "true" : "false") +
        ",\"dataState\":\"" + dataState + "\"" +
        ",\"uptime\":" + millis() + ",\"mappingCount\":" + uint32_t(POWERQUEEN_REGISTER_COUNT) +
        ",\"lastResponse\":" + (powerqueen.hasResponse() ? String(powerqueen.lastResponse()) : String("null")) +
        ",\"lastUpdate\":" + (d.lastUpdate ? String(d.lastUpdate) : String("null")) +
        ",\"battery\":{\"voltage\":" + number(d.batteryVoltage) + ",\"current\":" + number(d.batteryCurrent) +
        ",\"power\":" + number(d.batteryPower) + ",\"soc\":" + number(d.batterySoc) +
        ",\"temperature\":" + number(d.batteryTemperature) +
        "},\"pv\":{\"voltage\":" + number(d.pvVoltage) + ",\"current\":" + number(d.pvCurrent) +
        ",\"power\":" + number(d.pvPower) +
        "},\"load\":{\"voltage\":" + number(d.loadVoltage) + ",\"current\":" + number(d.loadCurrent) +
        ",\"power\":" + number(d.loadPower) +
        "},\"controller\":{\"temperature\":" + number(d.controllerTemperature) +
        ",\"chargeState\":" + (d.chargeStateValid ? String(d.chargeState) : String("null")) +
        ",\"faultCode\":" + (d.faultCodeValid ? String(d.faultCode) : String("null")) +
        "},\"energy\":{\"daily\":" + number(d.dailyYieldWh) + ",\"total\":" + number(d.totalYieldWh) +
        "},\"stats\":" + statsJson() + "}";
}
static void json(const String& body, int code = 200) {
    server.sendHeader("Cache-Control", "no-store"); server.send(code, "application/json", body);
}
static bool authorized() {
    if (server.header("X-Scan-Token") == scanToken) return true;
    json("{\"error\":\"Scan token required\"}", 403); return false;
}
static bool parse(const String& s, uint32_t& value) {
    if (!s.length() || s.length() > 7 || s[0] == '-' || s[0] == '+') return false;
    char* end; value = strtoul(s.c_str(), &end, s.startsWith("0x") || s.startsWith("0X") ? 16 : 10);
    return *end == 0;
}
static void asset(const char* path, const char* type) {
    if (!fsReady || !LittleFS.exists(path)) {
        server.send(503, "text/plain; charset=utf-8", "Webdateien fehlen. Bitte pio run -t uploadfs ausfuehren."); return;
    }
    File file = LittleFS.open(path, "r"); server.streamFile(file, type); file.close();
}
void webBegin() {
    fsReady = LittleFS.begin(false); // Never silently erase the filesystem.
    char token[33]; snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
        (unsigned long)esp_random(), (unsigned long)esp_random(),
        (unsigned long)esp_random(), (unsigned long)esp_random()); scanToken = token;
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    if (strlen(WIFI_SSID)) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiStarted = millis();
    const char* headers[] = {"X-Scan-Token"}; server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, [] { asset("/index.html", "text/html; charset=utf-8"); });
    server.on("/debug", HTTP_GET, [] { asset("/index.html", "text/html; charset=utf-8"); });
    server.on("/style.css", HTTP_GET, [] { asset("/style.css", "text/css"); });
    server.on("/app.js", HTTP_GET, [] { asset("/app.js", "application/javascript"); });
    server.on("/api/status", HTTP_GET, [] { json(statusJson()); });
    server.on("/api/modbus/raw", HTTP_GET, [] { json(rawJson()); });
    server.on("/api/modbus/stats", HTTP_GET, [] { json(statsJson()); });
    server.on("/api/modbus/control", HTTP_GET, [] { json(String("{\"token\":\"") + scanToken + "\"}"); });
    server.on("/api/modbus/scan", HTTP_POST, [] {
        if (!authorized()) return;
        uint32_t start, end, fc, count = 1;
        if (!parse(server.arg("start"), start) || !parse(server.arg("end"), end) ||
            !parse(server.arg("function"), fc) ||
            (server.hasArg("count") && !parse(server.arg("count"), count)) ||
            start > end || end > 65535 || (fc != 3 && fc != 4) || count < 1 || count > SCAN_MAX_REGISTERS) {
            json("{\"error\":\"Invalid scan range/function/count\"}", 400); return;
        }
        if (!startScan(start, end, fc, count)) json("{\"error\":\"Busy\"}", 409);
        else json("{\"started\":true}", 202);
    });
    server.on("/api/modbus/stop", HTTP_POST, [] { if (authorized()) { stopMonitorJob(); json("{\"stopping\":true}"); } });
    server.on("/api/modbus/diagnose", HTTP_POST, [] {
        if (!authorized()) return;
        if (startDiagnosis()) json("{\"started\":true}", 202); else json("{\"error\":\"Busy\"}", 409);
    });
    server.on("/api/history", HTTP_GET, [] {
        server.sendHeader("Cache-Control", "no-store");
        server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "application/json", "");
        server.sendContent("[");
        for (uint16_t i = 0; i < powerqueen.historyCount(); ++i) {
            const auto& h = powerqueen.sample(i);
            server.sendContent(String(i ? ",{" : "{") + "\"timestamp\":" + h.timestamp +
                ",\"pvPower\":" + number(h.pvPower) + ",\"batteryVoltage\":" + number(h.batteryVoltage) +
                ",\"batteryCurrent\":" + number(h.batteryCurrent) + ",\"temperature\":" + number(h.temperature) + "}");
            yield();
        }
        server.sendContent("]"); server.sendContent("");
    });
    server.onNotFound([] { server.send(404, "text/plain", "Not found"); }); server.begin();
}
void webTick() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !hadWifi) { Serial.print("WiFi: connected IP: "); Serial.println(WiFi.localIP()); }
    if (!connected && hadWifi) wifiStarted = millis();
    hadWifi = connected;
    if (!connected && !apStarted && uint32_t(millis() - wifiStarted) >= WIFI_FALLBACK_MS) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
        apStarted = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        if (apStarted) Serial.println("WiFi fallback AP: PowerQueen-Setup IP: 192.168.4.1");
    }
    // Do not let a synchronous HTTP transfer delay an active RTU receive window.
    if (!modbus.busy()) server.handleClient();
}
