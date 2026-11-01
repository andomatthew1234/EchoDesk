#include "echodesk_wifi_setup.h"

#if defined(ECHODESK_OS)

#include "config.h"
#include "config_store.h"
#include "echodesk_setup.h"
#include "echodesk_wifi_protocol.h"
#include "serial_receiver.h"
#include "ui_echodesk.h"
#include "wifi_time.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <string.h>

namespace {

constexpr unsigned long PROVISIONING_LIFETIME_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long CLIENT_IDLE_TIMEOUT_MS = 15000;
constexpr char DEVICE_NAME[] = "EchoDesk";
constexpr char DEVICE_MODEL[] = "Freenove FNK0114A_2P4";

WiFiServer service_server(ECHODESK_WIFI_SERVICE_PORT);
WiFiUDP discovery_udp;
WiFiClient service_client;
EchoDeskWifiSetupStatus status = EchoDeskWifiSetupStatus::IDLE;
bool start_requested = false;
bool udp_started = false;
bool server_started = false;
unsigned long provisioning_started_at = 0;
unsigned long client_last_byte_at = 0;
unsigned long restart_at = 0;
char *message_buffer = nullptr;
size_t message_length = 0;
char ap_ssid[18] = {};
char pairing_code[ECHODESK_WIFI_PAIRING_CODE_LENGTH + 1] = {};
char device_id[13] = {};

bool secure_equals(const char *left, const char *right, size_t length) {
    if (!left || !right) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    }
    return difference == 0 && left[length] == '\0' && right[length] == '\0';
}

void make_identity() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(ap_ssid, sizeof(ap_ssid), "EchoDesk-%02X%02X", mac[4], mac[5]);
}

void make_pairing_code() {
    const uint32_t value = esp_random() % 100000000UL;
    snprintf(pairing_code, sizeof(pairing_code), "%08lu",
             static_cast<unsigned long>(value));
}

void stop_client() {
    if (service_client) service_client.stop();
    message_length = 0;
    if (message_buffer) message_buffer[0] = '\0';
}

void stop_network_services(bool stop_radio) {
    stop_client();
    if (udp_started) {
        discovery_udp.stop();
        udp_started = false;
    }
    if (server_started) {
        service_server.end();
        server_started = false;
    }
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
    }
    wifi_time_set_provisioning(false);
    if (stop_radio && !wifi_time_has_credentials()) WiFi.mode(WIFI_OFF);
}

void ensure_common_services() {
    if (!server_started) {
        service_server.begin();
        service_server.setNoDelay(true);
        server_started = true;
    }
    if (!udp_started) {
        udp_started = discovery_udp.begin(ECHODESK_WIFI_DISCOVERY_PORT) == 1;
        if (!udp_started) Serial.println("[EchoDesk WiFi] UDP discovery failed to start");
    }
}

void start_provisioning() {
    stop_network_services(false);
    if (!echodesk_wifi_valid_pairing_code(pairing_code)) make_pairing_code();
    wifi_time_set_provisioning(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    if (!WiFi.softAP(ap_ssid, pairing_code)) {
        status = EchoDeskWifiSetupStatus::ERROR;
        wifi_time_set_provisioning(false);
        Serial.println("[EchoDesk WiFi] Could not start temporary setup network");
        return;
    }
    ensure_common_services();
    provisioning_started_at = millis();
    status = EchoDeskWifiSetupStatus::PROVISIONING;
    Serial.printf("[EchoDesk WiFi] Temporary setup network '%s' active for 15 minutes\n",
                  ap_ssid);
}

void write_json(WiFiClient &client, JsonDocument &document) {
    serializeJson(document, client);
    client.print('\n');
    client.flush();
}

void write_error(WiFiClient &client, const char *code, const char *message) {
    JsonDocument response;
    response["type"] = "echodesk_provision_result";
    response["version"] = ECHODESK_WIFI_PROTOCOL_VERSION;
    response["ok"] = false;
    response["code"] = code;
    response["message"] = message;
    write_json(client, response);
}

void write_identity(JsonDocument &response, const char *nonce, bool setup_mode) {
    response["type"] = "echodesk_device";
    response["version"] = ECHODESK_WIFI_PROTOCOL_VERSION;
    response["nonce"] = nonce;
    response["deviceId"] = device_id;
    response["name"] = DEVICE_NAME;
    response["model"] = DEVICE_MODEL;
    response["firmwareVersion"] = APP_VERSION;
    response["setupState"] = echodesk_setup_state_name(g_config.setup_state);
    response["setupMode"] = setup_mode;
    response["paired"] = !setup_mode && wifi_time_has_client_token();
    response["ip"] = setup_mode ? WiFi.softAPIP().toString()
                                : WiFi.localIP().toString();
    response["port"] = ECHODESK_WIFI_SERVICE_PORT;
}

void complete_provisioning(JsonDocument &request, WiFiClient &client) {
    if (request.size() != 6 ||
        strcmp(request["type"] | "", "echodesk_provision") != 0 ||
        !request["version"].is<uint8_t>() ||
        request["version"].as<uint8_t>() != ECHODESK_WIFI_PROTOCOL_VERSION) {
        write_error(client, "invalid_request", "version 1 provisioning message required");
        return;
    }
    const char *code = request["pairingCode"] | "";
    const char *ssid = request["ssid"] | "";
    const char *password = request["password"] | "";
    const char *token = request["clientToken"] | "";
    if (!echodesk_wifi_valid_pairing_code(code) ||
        !secure_equals(code, pairing_code, ECHODESK_WIFI_PAIRING_CODE_LENGTH)) {
        write_error(client, "pairing_failed", "pairing code is incorrect");
        return;
    }
    if (!echodesk_wifi_valid_ssid(ssid) ||
        !echodesk_wifi_valid_password(password) ||
        !echodesk_wifi_valid_client_token(token)) {
        write_error(client, "invalid_credentials", "SSID, password or client token is invalid");
        return;
    }
    if (!wifi_time_save_client_token(token)) {
        write_error(client, "storage_failed", "could not persist pairing token");
        return;
    }
    if (!wifi_time_store_credentials(ssid, password)) {
        write_error(client, "storage_failed", "could not persist Wi-Fi credentials");
        return;
    }

    g_config.setup_state = ECHODESK_SETUP_COMPLETED;
    g_config.setup_transport = ECHODESK_SETUP_TRANSPORT_WIFI;
    g_config.setup_version = ECHODESK_SETUP_VERSION;
    config_save(g_config);

    JsonDocument response;
    response["type"] = "echodesk_provision_result";
    response["version"] = ECHODESK_WIFI_PROTOCOL_VERSION;
    response["ok"] = true;
    response["deviceId"] = device_id;
    response["restarting"] = true;
    write_json(client, response);
    client.stop();

    serial_echodesk_state_changed();
    ui_echodesk_setup_state_changed();

    stop_network_services(false);
    status = EchoDeskWifiSetupStatus::CONNECTING;
    restart_at = millis() + 750;
    Serial.printf("[EchoDesk WiFi] Provisioned; connecting to '%s'\n", ssid);
}

void handle_identify(JsonDocument &request, WiFiClient &client) {
    if (request.size() != 4 ||
        strcmp(request["type"] | "", "echodesk_identify") != 0 ||
        !request["version"].is<uint8_t>() ||
        request["version"].as<uint8_t>() != ECHODESK_WIFI_PROTOCOL_VERSION) {
        write_error(client, "invalid_request", "version 1 identify message required");
        return;
    }
    const char *token = request["clientToken"] | "";
    const char *nonce = request["nonce"] | "";
    if (!echodesk_wifi_valid_client_token(token) ||
        !secure_equals(token, wifi_time_client_token(), ECHODESK_WIFI_CLIENT_TOKEN_LENGTH)) {
        write_error(client, "unauthorized", "client token is invalid");
        return;
    }
    if (!echodesk_wifi_valid_nonce(nonce)) {
        write_error(client, "invalid_request", "nonce is invalid");
        return;
    }
    JsonDocument response;
    write_identity(response, nonce, false);
    response["authenticated"] = true;
    write_json(client, response);
}

void process_service_message() {
    JsonDocument request;
    const DeserializationError error =
        deserializeJson(request, message_buffer, message_length);
    if (error || !request.is<JsonObject>()) {
        write_error(service_client, "invalid_json", "bounded JSON object required");
        return;
    }
    if (status == EchoDeskWifiSetupStatus::PROVISIONING) {
        complete_provisioning(request, service_client);
    } else {
        handle_identify(request, service_client);
    }
}

void service_tcp() {
    if (!server_started) return;
    if (!message_buffer) return;
    if (!service_client || !service_client.connected()) {
        stop_client();
        service_client = service_server.available();
        if (service_client) {
            service_client.setNoDelay(true);
            client_last_byte_at = millis();
        }
        return;
    }
    while (service_client.available()) {
        const int value = service_client.read();
        client_last_byte_at = millis();
        if (value == '\n') {
            process_service_message();
            stop_client();
            return;
        }
        if (value == '\r') continue;
        if (value == 0) {
            write_error(service_client, "invalid_json", "NUL bytes are not allowed");
            stop_client();
            return;
        }
        if (message_length >= ECHODESK_WIFI_MAX_MESSAGE_BYTES) {
            write_error(service_client, "message_too_large", "message exceeds 1024 bytes");
            stop_client();
            return;
        }
        message_buffer[message_length++] = static_cast<char>(value);
        message_buffer[message_length] = '\0';
    }
    if (millis() - client_last_byte_at > CLIENT_IDLE_TIMEOUT_MS) stop_client();
}

void service_discovery() {
    if (!udp_started) return;
    const int packet_size = discovery_udp.parsePacket();
    if (packet_size <= 0) return;
    if (packet_size > ECHODESK_WIFI_MAX_MESSAGE_BYTES) {
        while (discovery_udp.available()) discovery_udp.read();
        return;
    }
    char discovery_buffer[ECHODESK_WIFI_MAX_MESSAGE_BYTES + 1];
    const int read = discovery_udp.read(discovery_buffer, packet_size);
    if (read <= 0) return;
    discovery_buffer[read] = '\0';
    JsonDocument request;
    if (deserializeJson(request, discovery_buffer, static_cast<size_t>(read)) ||
        !request.is<JsonObject>() || request.size() != 3 ||
        strcmp(request["type"] | "", "echodesk_discover") != 0 ||
        !request["version"].is<uint8_t>() ||
        request["version"].as<uint8_t>() != ECHODESK_WIFI_PROTOCOL_VERSION) {
        return;
    }
    const char *nonce = request["nonce"] | "";
    if (!echodesk_wifi_valid_nonce(nonce)) return;
    JsonDocument response;
    const bool setup_mode = status == EchoDeskWifiSetupStatus::PROVISIONING;
    write_identity(response, nonce, setup_mode);
    discovery_udp.beginPacket(discovery_udp.remoteIP(), discovery_udp.remotePort());
    serializeJson(response, discovery_udp);
    discovery_udp.endPacket();
}

} // namespace

void echodesk_wifi_setup_init() {
    if (!message_buffer) {
        message_buffer = static_cast<char *>(malloc(ECHODESK_WIFI_MAX_MESSAGE_BYTES + 1));
        if (!message_buffer) {
            status = EchoDeskWifiSetupStatus::ERROR;
            Serial.println("[EchoDesk WiFi] Could not allocate bounded message buffer");
            return;
        }
        message_buffer[0] = '\0';
    }
    make_identity();
    if (g_config.setup_transport == ECHODESK_SETUP_TRANSPORT_WIFI &&
        echodesk_setup_normalize_state(g_config.setup_state) != ECHODESK_SETUP_COMPLETED) {
        echodesk_wifi_setup_request_start();
    } else if (g_config.setup_transport == ECHODESK_SETUP_TRANSPORT_WIFI &&
               echodesk_setup_normalize_state(g_config.setup_state) ==
                   ECHODESK_SETUP_COMPLETED &&
               wifi_time_has_client_token() && wifi_time_has_credentials()) {
        status = EchoDeskWifiSetupStatus::CONNECTING;
    }
}

void echodesk_wifi_setup_tick() {
    if (restart_at != 0 && static_cast<long>(millis() - restart_at) >= 0) {
        Serial.flush();
        ESP.restart();
    }
    if (start_requested) {
        start_requested = false;
        start_provisioning();
    }
    if (status == EchoDeskWifiSetupStatus::PROVISIONING) {
        service_discovery();
        service_tcp();
        if (millis() - provisioning_started_at >= PROVISIONING_LIFETIME_MS) {
            stop_network_services(true);
            status = EchoDeskWifiSetupStatus::EXPIRED;
            pairing_code[0] = '\0';
            Serial.println("[EchoDesk WiFi] Temporary setup network expired");
        }
        return;
    }
    if ((status == EchoDeskWifiSetupStatus::CONNECTING ||
         status == EchoDeskWifiSetupStatus::CONNECTED) &&
        wifi_time_is_connected() && wifi_time_has_client_token()) {
        status = EchoDeskWifiSetupStatus::CONNECTED;
        ensure_common_services();
        service_discovery();
        service_tcp();
    }
}

void echodesk_wifi_setup_request_start() {
    if (status == EchoDeskWifiSetupStatus::PROVISIONING || start_requested) return;
    make_pairing_code();
    status = EchoDeskWifiSetupStatus::STARTING;
    start_requested = true;
}

void echodesk_wifi_setup_stop() {
    start_requested = false;
    stop_network_services(true);
    status = EchoDeskWifiSetupStatus::IDLE;
    pairing_code[0] = '\0';
}

EchoDeskWifiSetupStatus echodesk_wifi_setup_status() { return status; }
const char *echodesk_wifi_setup_ap_ssid() { return ap_ssid; }
const char *echodesk_wifi_setup_pairing_code() { return pairing_code; }
const char *echodesk_wifi_setup_device_id() { return device_id; }
bool echodesk_wifi_setup_is_active() {
    return status == EchoDeskWifiSetupStatus::STARTING ||
           status == EchoDeskWifiSetupStatus::PROVISIONING;
}

#else

void echodesk_wifi_setup_init() {}
void echodesk_wifi_setup_tick() {}
void echodesk_wifi_setup_request_start() {}
void echodesk_wifi_setup_stop() {}
EchoDeskWifiSetupStatus echodesk_wifi_setup_status() { return EchoDeskWifiSetupStatus::IDLE; }
const char *echodesk_wifi_setup_ap_ssid() { return ""; }
const char *echodesk_wifi_setup_pairing_code() { return ""; }
const char *echodesk_wifi_setup_device_id() { return ""; }
bool echodesk_wifi_setup_is_active() { return false; }

#endif
