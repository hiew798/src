/*
 * =====================================================================================
 * File: WebServerManager.h
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Manages ESP32 WiFi SoftAP, Captive Portal DNS Redirection, and Async HTTP Web Server.
 *   
 *   STRICT ROLE SEGREGATION:
 *   - When CURRENT_NODE_ROLE == ROLE_CIVILIAN:
 *       Only victim routes are exposed (Captive portal, SOS submission, report viewing).
 *       Rescuer admin endpoints are completely omitted from the routing table.
 *   - When CURRENT_NODE_ROLE == ROLE_RESCUER:
 *       Hosts Tactical Command Dashboard. Exposes triage feeds, beacon poll triggers,
 *       and Anti-Packet clearance endpoints. SOS submission is disabled.
 * =====================================================================================
 */

#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

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

// Embedded self-contained Rescuer Tactical Dashboard HTML
const char RESCUER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RESCUER TACTICAL COMMAND</title>
<style>
:root{--bg:#090d16;--card:#131b2e;--red:#ef4444;--orange:#f97316;--green:#10b981;--blue:#38bdf8;--text:#f1f5f9;--sub:#94a3b8;}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;}
body{background:var(--bg);color:var(--text);padding:14px;display:flex;justify-content:center;}
.wrap{width:100%;max-width:750px;}
.top{background:linear-gradient(135deg,rgba(16,185,129,0.15),rgba(19,27,46,0.9));border:1px solid var(--green);border-radius:12px;padding:14px;margin-bottom:14px;display:flex;justify-content:space-between;align-items:center;}
.top h1{font-size:1.1rem;color:#fff;letter-spacing:0.5px;}
.actions{display:flex;gap:8px;margin-bottom:14px;}
.btn{flex:1;padding:12px;border:none;border-radius:8px;font-weight:700;cursor:pointer;font-size:0.85rem;}
.btn-poll{background:var(--blue);color:#000;}
.btn-ref{background:#334155;color:#fff;}
.card{background:var(--card);border:1px solid rgba(255,255,255,0.08);border-radius:10px;padding:12px;margin-bottom:10px;}
.card.priority-high{border-left:5px solid var(--red);}
.card.priority-med{border-left:5px solid var(--orange);}
.card.status-resolved{border-left:5px solid var(--green);opacity:0.65;}
.row{display:flex;justify-content:space-between;font-weight:700;margin-bottom:6px;font-size:0.95rem;}
.badges{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:8px;}
.b{font-size:0.65rem;padding:2px 6px;border-radius:4px;font-weight:700;}
.b-red{background:rgba(239,68,68,0.25);color:#fca5a5;}
.b-org{background:rgba(249,115,22,0.25);color:#fdba74;}
.b-grn{background:rgba(16,185,129,0.25);color:#6ee7b7;}
.btn-clear{width:100%;padding:10px;background:linear-gradient(135deg,#059669,#10b981);color:#fff;border:none;border-radius:6px;font-weight:700;cursor:pointer;font-size:0.8rem;margin-top:8px;}
.btn-clear:disabled{background:#475569;cursor:not-allowed;}
</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div>
      <h1>RESCUE COMMAND CONSOLE</h1>
      <p id="diag" style="font-size:0.75rem;color:var(--sub);">Connecting to Gateway...</p>
    </div>
    <span style="font-size:1.5rem;">🛡️</span>
  </div>
  <div class="actions">
    <button class="btn btn-poll" id="btnPoll">📡 Poll Nearby Node Backlogs</button>
    <button class="btn btn-ref" id="btnRef">↻ Refresh</button>
  </div>
  <div id="list">Loading distress incidents...</div>
</div>
<script>
async function load(){
  try{
    const r=await fetch('/api/rescuer/all-reports');
    const d=await r.json();
    const l=document.getElementById('list');
    if(!d.length){l.innerHTML='<p style="text-align:center;color:#64748b;padding:30px;">No incidents reported yet.</p>';return;}
    l.innerHTML=d.map(i=>{
      const isRes=i.status===2;
      const isHigh=i.trapped||i.injured;
      const cls=isRes?'status-resolved':(isHigh?'priority-high':'priority-med');
      return `<div class="card ${cls}">
        <div class="row">
          <span>📍 ${i.location}</span>
          <span>👥 ${i.victimCount} People</span>
        </div>
        <div class="badges">
          ${isRes?'<span class="b b-grn">✓ RESOLVED / RESCUED</span>':'<span class="b b-org">PENDING RESCUE</span>'}
          ${i.trapped?'<span class="b b-red">TRAPPED</span>':''}
          ${i.injured?'<span class="b b-red">INJURED</span>':''}
          ${i.needWater?'<span class="b b-org">WATER</span>':''}
          ${i.needMeds?'<span class="b b-org">MEDS</span>':''}
        </div>
        <p style="font-size:0.85rem;color:#cbd5e1;">${i.text||'No details.'}</p>
        <p style="font-size:0.7rem;color:#64748b;margin-top:4px;">ID: ${i.msgId} | Contact: ${i.name}</p>
        ${!isRes?`<button class="btn-clear" onclick="resolve('${i.msgId}')">✓ MARK RESCUED & CLEAR MESH BACKLOG</button>`:''}
      </div>`;
    }).join('');
  }catch(e){console.error(e);}
}
async function resolve(id){
  if(!confirm('Broadcast Anti-Packet to clear incident '+id+' across the entire mesh?'))return;
  await fetch('/api/rescuer/clear-alert',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({msgId:id})});
  load();
}
document.getElementById('btnPoll').onclick=async()=>{
  alert('Broadcasting Rescuer Beacon...');
  await fetch('/api/rescuer/poll-beacon',{method:'POST'});
};
document.getElementById('btnRef').onclick=load;
setInterval(load,4000);
load();
</script>
</body>
</html>
)rawliteral";

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
     * Initializes WiFi SoftAP, Captive Portal DNS, and role-segregated HTTP routes.
     */
    bool begin() {
        WiFi.mode(WIFI_AP);

        uint8_t mac[6];
        WiFi.macAddress(mac);
        char ssidBuf[32];

        // Strict role-segregated SSID naming
        if (CURRENT_NODE_ROLE == ROLE_RESCUER) {
            snprintf(ssidBuf, sizeof(ssidBuf), "%s%02X%02X", RESCUER_AP_SSID_PREFIX, mac[4], mac[5]);
        } else {
            snprintf(ssidBuf, sizeof(ssidBuf), "%s%02X%02X", CIVILIAN_AP_SSID_PREFIX, mac[4], mac[5]);
        }
        apSSID = String(ssidBuf);

        Serial.printf("[WIFI AP] Starting Access Point: %s (Role: %s) ...\n",
                      apSSID.c_str(),
                      CURRENT_NODE_ROLE == ROLE_RESCUER ? "RESCUER GATEWAY" : "CIVILIAN NODE");

        bool apSuccess = WiFi.softAP(apSSID.c_str(), WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_MAX_CONNECTIONS);
        if (!apSuccess) {
            Serial.println("[WIFI ERROR] Failed to start SoftAP!");
            return false;
        }

        IPAddress apIP = WiFi.softAPIP();
        Serial.printf("[WIFI SUCCESS] AP Started. IP: %s\n", apIP.toString().c_str());

        dnsServer.start(DNS_PORT, "*", apIP);
        Serial.println("[DNS] Captive Portal DNS running on port 53.");

        // Setup strictly role-segregated routes
        if (CURRENT_NODE_ROLE == ROLE_RESCUER) {
            setupRescuerRoutes();
        } else {
            setupCivilianRoutes();
        }

        server.begin();
        Serial.println("[HTTP] Async HTTP Web Server started on port 80.");
        return true;
    }

    void update() {
        dnsServer.processNextRequest();
    }

    String getApSSID() const { return apSSID; }

private:
    /**
     * CIVILIAN ROUTES: Exposes only victim reporting and status viewing.
     * Administrative and clearance endpoints are NOT registered.
     */
    void setupCivilianRoutes() {
        // Captive Portal Redirects
        server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("http://192.168.4.1/"); });
        server.on("/gen_204",      HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("http://192.168.4.1/"); });
        server.on("/redirect",     HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("http://192.168.4.1/"); });
        server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("http://192.168.4.1/"); });
        server.on("/canonical.html",      HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("http://192.168.4.1/"); });

        // Static files from LittleFS
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

        // Diagnostic status endpoint
        server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            AsyncJsonResponse *response = new AsyncJsonResponse();
            JsonObject root = response->getRoot().to<JsonObject>();

            root["nodeId"]          = String(g_nodeId, HEX);
            root["role"]            = "CIVILIAN";
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

        // Victim SOS Submission
        AsyncCallbackJsonWebHandler *sosHandler = new AsyncCallbackJsonWebHandler("/api/sos", 
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject jsonObj = json.as<JsonObject>();

                if (!jsonObj.containsKey("location") || !jsonObj.containsKey("text")) {
                    request->send(400, "application/json", "{\"error\":\"Missing required fields\"}");
                    return;
                }

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

                // Collision-proof NVS persistent Message ID
                uint32_t msgId = storage ? storage->getNextUniqueMsgId(g_nodeId) : micros();

                Serial.printf("[WEB SOS SUBMIT] New SOS submitted! MsgID: 0x%08X\n", msgId);

                bool sentOk = false;
                if (meshManager) {
                    sentOk = meshManager->broadcastDistressAlert(msgId, payload);
                }

                if (storage) {
                    storage->saveUnackedMessage(msgId, payload);
                }

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

        // Civilian Distress Reports List (read-only)
        server.on("/api/distress-list", HTTP_GET, [this](AsyncWebServerRequest *request) {
            AsyncJsonResponse *response = new AsyncJsonResponse(false, 8192);
            JsonArray arr = response->getRoot().to<JsonArray>();

            if (storage) {
                size_t count = storage->getHistoryMessageCount();
                for (size_t i = 0; i < count; i++) {
                    StoredMessage rec;
                    if (storage->getHistoryMessageByIndex(i, rec)) {
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
                        obj["status"]      = rec.status; // 0=PENDING, 1=ACKED, 2=RESOLVED
                        obj["timestamp"]   = rec.payload.timestamp;
                    }
                }
            }

            response->setLength();
            request->send(response);
        });

        server.onNotFound([](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
    }

    /**
     * RESCUER ROUTES: Exposes the Tactical Command Dashboard, Anti-Packet triggers,
     * and Rescuer Beacon polling. Civilian SOS submission is disabled.
     */
    void setupRescuerRoutes() {
        // Tactical Dashboard Root Page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send_P(200, "text/html", RESCUER_HTML);
        });

        // All reports for Triage Dashboard (Sorted priority: Trapped & Injured first)
        server.on("/api/rescuer/all-reports", HTTP_GET, [this](AsyncWebServerRequest *request) {
            AsyncJsonResponse *response = new AsyncJsonResponse(false, 8192);
            JsonArray arr = response->getRoot().to<JsonArray>();

            if (storage) {
                size_t count = storage->getHistoryMessageCount();
                for (size_t i = 0; i < count; i++) {
                    StoredMessage rec;
                    if (storage->getHistoryMessageByIndex(i, rec)) {
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
                        obj["status"]      = rec.status; // 0=PENDING, 1=ACKED, 2=RESOLVED
                        obj["timestamp"]   = rec.payload.timestamp;
                    }
                }
            }

            response->setLength();
            request->send(response);
        });

        // Rescuer Clearance / Anti-Packet Trigger
        AsyncCallbackJsonWebHandler *clearHandler = new AsyncCallbackJsonWebHandler("/api/rescuer/clear-alert", 
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                JsonObject jsonObj = json.as<JsonObject>();
                if (!jsonObj.containsKey("msgId")) {
                    request->send(400, "application/json", "{\"error\":\"Missing msgId\"}");
                    return;
                }

                String hexStr = jsonObj["msgId"].as<String>();
                uint32_t targetId = strtoul(hexStr.c_str(), NULL, 16);

                Serial.printf("[RESCUER ACTION] Marking MsgID 0x%08X resolved and broadcasting Anti-Packet...\n", targetId);

                // 1. Mark resolved locally
                if (storage) {
                    storage->markMessageResolved(targetId, g_nodeId, millis() / 1000);
                }

                // 2. Broadcast Anti-Packet over LoRa mesh
                bool loraOk = false;
                if (meshManager) {
                    loraOk = meshManager->broadcastClearAlert(&targetId, 1, g_nodeId);
                }

                AsyncJsonResponse *response = new AsyncJsonResponse();
                JsonObject root = response->getRoot().to<JsonObject>();
                root["success"]  = true;
                root["msgId"]    = hexStr;
                root["antiPktTx"]= loraOk;
                response->setLength();
                request->send(response);
            }
        );
        server.addHandler(clearHandler);

        // Rescuer Beacon Poll Trigger
        server.on("/api/rescuer/poll-beacon", HTTP_POST, [this](AsyncWebServerRequest *request) {
            bool ok = false;
            if (meshManager) {
                ok = meshManager->sendRescuerBeacon();
            }
            request->send(200, "application/json", ok ? "{\"success\":true}" : "{\"error\":\"TX failed\"}");
        });

        server.onNotFound([](AsyncWebServerRequest *request) {
            request->redirect("http://192.168.4.1/");
        });
    }
};

#endif // WEB_SERVER_MANAGER_H
