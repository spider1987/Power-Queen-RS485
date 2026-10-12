#include "mqtt.h"
#include "config.h"
#include "powerqueen.h"
#include "monitor.h"
#include "webserver_api.h"
#include <WiFi.h>
#include <PubSubClient.h>

class BoundedWiFiClient : public WiFiClient {
public:
    int connect(const char* host, uint16_t port) override { return WiFiClient::connect(host, port, 500); }
    int connect(IPAddress ip, uint16_t port) override { return WiFiClient::connect(ip, port, 500); }
};
static BoundedWiFiClient network;
static PubSubClient broker(network);
static uint32_t lastConnect = 0, lastPublish = 0;
static bool tried = false;
static void publish(const char* suffix, const String& value) {
    const String topic = String(MQTT_PREFIX) + "/" + suffix;
    broker.publish(topic.c_str(), value.c_str(), true);
}
static void measurement(const char* suffix, float value) {
    // Explicit null clears old retained measurements when values become unknown.
    publish(suffix, isfinite(value) ? String(value, 3) : String("null"));
}
void mqttBegin() {
    if (!MQTT_ENABLED) return;
    broker.setServer(MQTT_HOST, MQTT_PORT); broker.setBufferSize(3072);
    broker.setSocketTimeout(1);
}
void mqttTick() {
    if (!MQTT_ENABLED || !strlen(MQTT_HOST) || WiFi.status() != WL_CONNECTED || modbus.busy()) return;
    if (!broker.connected()) {
        if (tried && uint32_t(millis() - lastConnect) < 15000UL) return;
        lastConnect = millis(); tried = true;
        const String client = String("powerqueen-") + String(uint32_t(ESP.getEfuseMac()), HEX);
        const String availability = String(MQTT_PREFIX) + "/status";
        if (!broker.connect(client.c_str(), strlen(MQTT_USER) ? MQTT_USER : nullptr,
                strlen(MQTT_USER) ? MQTT_PASSWORD : nullptr, availability.c_str(), 0, true, "offline")) return;
        lastPublish = millis() - 10000UL;
    }
    broker.loop();
    if (uint32_t(millis() - lastPublish) < 10000UL) return;
    lastPublish = millis();
    const auto& d = powerqueen.data;
    const bool fresh = d.online && uint32_t(millis() - d.lastUpdate) < DATA_STALE_MS;
    publish("status", powerqueen.communicationState());
    measurement("battery/voltage", fresh ? d.batteryVoltage : NAN);
    measurement("battery/current", fresh ? d.batteryCurrent : NAN);
    measurement("battery/power", fresh ? d.batteryPower : NAN);
    measurement("pv/voltage", fresh ? d.pvVoltage : NAN);
    measurement("pv/current", fresh ? d.pvCurrent : NAN);
    measurement("pv/power", fresh ? d.pvPower : NAN);
    measurement("controller/temperature", fresh ? d.controllerTemperature : NAN);
    publish("controller/state", fresh && d.chargeStateValid ? String(d.chargeState) : String("null"));
    publish("controller/fault", fresh && d.faultCodeValid ? String(d.faultCode) : String("null"));
    measurement("energy/daily", fresh ? d.dailyYieldWh : NAN);
    measurement("energy/total", fresh ? d.totalYieldWh : NAN);
    publish("raw", rawJson());
}
