/*
 * =====================================================================================
 * File: WebServerManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Manages ESP32 WiFi Access Point, Captive Portal DNS Redirection, and Async HTTP Web Server.
 *   
 *   Features:
 *   - Creates open WiFi AP ("EMERGENCY_NODE_XXXX").
 *   - Runs DNS server to intercept all domain requests (Captive Portal).
 *   - Automatically pops up Emergency Web Portal on connected Android devices.
 *   - Serves web UI files directly from LittleFS flash storage.
 *   - Asynchronous non-blocking HTTP endpoints (`/api/sos`, `/api/status`, `/api/distress-list`).
 * =====================================================================================
 */

#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

//to get the MAC
#include "esp_mac.h"

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "PacketDefs.h"
#include "StorageManager.h"
#include "LoRaMeshManager.h"

class WebServerManager {
private:
    AsyncWebServer server;
    DNSServer dnsServer;
    StorageManager* storage;
    LoRaMeshManager* meshManager;
    String apSSID;

public:
    WebServerManager(StorageManager* storageMgr, LoRaMeshManager* meshMgr) 
        : server(HTTP_PORT), storage(storageMgr), meshManager(meshMgr) {}

    /**
     * Initializes WiFi SoftAP, Captive Portal DNS redirection, and HTTP routes.
     */
    bool begin() {
        // Configure WiFi Access Point mode first so MAC address is correctly loaded
        WiFi.mode(WIFI_AP);

        // Build unique SSID using lower 2 bytes of ESP32 WiFi MAC address
        uint8_t mac[6];

        // WiFi.macAddress(mac);
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

        char ssidBuf[32];
        snprintf(ssidBuf, sizeof(ssidBuf), "%s%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
        apSSID = String(ssidBuf);

        Serial.printf("[WIFI AP] Starting Access Point: %s ...\n", apSSID.c_str());

        // Start WiFi Access Point
        bool apSuccess = WiFi.softAP(apSSID.c_str(), WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_MAX_CONNECTIONS);
        if (!apSuccess) {
            Serial.println("[WIFI ERROR] Failed to start SoftAP!");
            return false;
        }

        IPAddress apIP = WiFi.softAPIP(); // Default: 192.168.4.1
        Serial.printf("[WIFI SUCCESS] AP Started. IP Address: %s\n", apIP.toString().c_str());

        // Setup Captive Portal DNS Server (Redirects ALL domain queries "*" to 192.168.4.1)
        dnsServer.start(DNS_PORT, "*", apIP);
        Serial.println("[DNS] Captive Portal DNS Server running on port 53.");

        // Setup HTTP Web Routes
        setupRoutes();

        // Start Web Server
        server.begin();
        Serial.println("[HTTP] Async HTTP Web Server started on port 80.");

        return true;
    }

    /**
     * Call this in the main loop to process Captive Portal DNS queries.
     */
    void update() {
        dnsServer.processNextRequest();
    }

    String getApSSID() const { return apSSID; }

private:
    /**
     * Registers all static file routes and REST API endpoints.
     */
    void setupRoutes() {
        // -----------------------------------------------------------------------------
        // CAPTIVE PORTAL REDIRECTION ROUTES FOR ANDROID / APPLE / WINDOWS
        // Android checks `/generate_204`, `/gen_204`, etc. to detect Captive Portals.
        // -----------------------------------------------------------------------------
        server.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
        server.on("/gen_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
        server.on("/redirect", HTTP_GET, [this](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
        server.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
        server.on("/canonical.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });

        // -----------------------------------------------------------------------------
        // STATIC FILE ROUTES (Served from LittleFS)
        // -----------------------------------------------------------------------------
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            if (LittleFS.exists("/index.html")) {
                request->send(LittleFS, "/index.html", "text/html");
            } else {
                request->send(200, "text/html", "<h2>Heltec Disaster Node</h2><p>Emergency UI loading...</p>");
            }
        });

        server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send(LittleFS, "/style.css", "text/css");
        });

        server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send(LittleFS, "/app.js", "application/javascript");
        });

        // -----------------------------------------------------------------------------
        // REST API: GET /api/status - Node Diagnostics
        // -----------------------------------------------------------------------------
        server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            AsyncJsonResponse *response = new AsyncJsonResponse();
            JsonObject root = response->getRoot().to<JsonObject>();

            root["nodeId"]          = String(g_nodeId, HEX);
            root["apSsid"]          = apSSID;
            root["connectedClients"]= WiFi.softAPgetStationNum();
            root["offlineSosCount"] = storage ? storage->getStoredMessageCount() : 0;
            root["lastRssi"]        = meshManager ? meshManager->getLastRssi() : 0;
            root["lastSnr"]         = meshManager ? meshManager->getLastSnr() : 0;
            root["maxHops"]         = LORA_MAX_HOP_COUNT;
            root["uptimeSec"]       = millis() / 1000;

            response->setLength();
            request->send(response);
        });

        // -----------------------------------------------------------------------------
        // REST API: POST /api/sos - Submit Victim Distress Alert
        // -----------------------------------------------------------------------------
        AsyncCallbackJsonWebHandler *sosHandler = new AsyncCallbackJsonWebHandler("/api/sos", 
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject jsonObj = json.as<JsonObject>();

                if (!jsonObj.containsKey("location") || !jsonObj.containsKey("text")) {
                    request->send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                    return;
                }

                // Fill binary DistressPayload struct from JSON submission
                DistressPayload payload;
                memset(&payload, 0, sizeof(DistressPayload));

                payload.statusFlags = 0;
                if (jsonObj["trapped"].as<bool>())    payload.statusFlags |= FLAG_TRAPPED;
                if (jsonObj["injured"].as<bool>())    payload.statusFlags |= FLAG_INJURED;
                if (jsonObj["needWater"].as<bool>())  payload.statusFlags |= FLAG_NEED_WATER;
                if (jsonObj["needMeds"].as<bool>())   payload.statusFlags |= FLAG_NEED_MEDS;

                payload.victimCount = jsonObj["victimCount"] | 1;

                strncpy(payload.floorRoom,   jsonObj["location"] | "Unknown", sizeof(payload.floorRoom) - 1);
                strncpy(payload.contactName, jsonObj["name"]     | "Anon",    sizeof(payload.contactName) - 1);
                strncpy(payload.textMsg,     jsonObj["text"]     | "",        sizeof(payload.textMsg) - 1);

                payload.timestamp = millis() / 1000;

                // Generate unique Message ID: (NodeId << 16) | (Sequence counter)
                static uint16_t localSeq = 0;
                uint32_t msgId = ((uint32_t)g_nodeId << 16) | (++localSeq);

                Serial.printf("[WEB SOS SUBMIT] New SOS received from WiFi client! MsgID: 0x%08X\n", msgId);

                // 1. Broadcast over LoRa mesh immediately
                bool sentOk = false;
                if (meshManager) {
                    sentOk = meshManager->broadcastDistressAlert(msgId, payload);
                }

                // 2. Store in LittleFS Flash memory (Store-and-Forward queue)
                if (storage) {
                    storage->saveUnackedMessage(msgId, payload);
                }

                // Send JSON response back to victim's browser
                AsyncJsonResponse *response = new AsyncJsonResponse();
                JsonObject root = response->getRoot().to<JsonObject>();
                root["success"] = true;
                root["msgId"]   = String(msgId, HEX);
                root["loraTx"]  = sentOk;
                root["saved"]   = true;

                response->setLength();
                request->send(response);
            }
        );
        server.addHandler(sosHandler);

        // -----------------------------------------------------------------------------
        // REST API: GET /api/distress-list - List All Local & Relayed Distress Alerts
        // -----------------------------------------------------------------------------
        server.on("/api/distress-list", HTTP_GET, [this](AsyncWebServerRequest *request) {
            AsyncJsonResponse *response = new AsyncJsonResponse(false, 8192); // Large json buffer
            JsonArray arr = response->getRoot().to<JsonArray>();

            if (storage) {
                size_t count = storage->getStoredMessageCount();
                for (size_t i = 0; i < count; i++) {
                    StoredMessage rec;
                    if (storage->getStoredMessageByIndex(i, rec)) {
                        JsonObject obj = arr.createNestedObject();
                        obj["msgId"]       = String(rec.msgId, HEX);
                        obj["location"]    = String(rec.payload.floorRoom);
                        obj["name"]        = String(rec.payload.contactName);
                        obj["text"]        = String(rec.payload.textMsg);
                        obj["victimCount"] = rec.payload.victimCount;
                        obj["trapped"]     = (rec.payload.statusFlags & FLAG_TRAPPED) != 0;
                        obj["injured"]     = (rec.payload.statusFlags & FLAG_INJURED) != 0;
                        obj["needWater"]   = (rec.payload.statusFlags & FLAG_NEED_WATER) != 0;
                        obj["needMeds"]    = (rec.payload.statusFlags & FLAG_NEED_MEDS) != 0;
                        obj["timestamp"]   = rec.payload.timestamp;
                    }
                }
            }

            response->setLength();
            request->send(response);
        });

        // Handler for non-existing routes (Captive Portal fallback)
        server.onNotFound([](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
    }
};

#endif // WEB_SERVER_MANAGER_H
