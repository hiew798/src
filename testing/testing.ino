#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Config.h"
#include "StorageManager.h"
#include "LoRaMeshManager.h"
#include "WebServerManager.h"

SET_LOOP_TASK_STACK_SIZE(LOOP_TASK_STACK_BYTES);

uint64_t g_nodeId=0;
StorageManager storageMgr;
LoRaMeshManager loraMesh(&storageMgr);
WebServerManager web(storageMgr,loraMesh);
void IRAM_ATTR setLoRaRxFlag() { loraMesh.handleInterruptFlag(); }

void setup() {
    Serial.begin(115200); delay(500);
    g_nodeId=ESP.getEfuseMac() & 0x0000ffffffffffffULL;
    WiFi.mode(WIFI_AP);
    String ssid=String(CURRENT_NODE_ROLE==ROLE_RESCUER ? "Heltec-Rescuer-" : "Heltec-LoRa-")+idString(g_nodeId).substring(10);
    WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
    // Both roles use an open Wi-Fi network; the rescuer dashboard has its own login.
    bool ap=WiFi.softAP(ssid.c_str(),nullptr,WIFI_AP_CHANNEL,false,4);
    Serial.printf("\n[BOOT] Protocol %u | %s | node %s\n",unsigned(MESH_VERSION),CURRENT_NODE_ROLE==ROLE_RESCUER ? "rescuer" : "civilian",idString(g_nodeId).c_str());
    Serial.printf("[FIRMWARE] %s | built %s %s | mode=%u (%s) | protocol=%u\n",
        FIRMWARE_REVISION,__DATE__,__TIME__,unsigned(TRANSFER_MODE),transferModeName(),unsigned(MESH_VERSION));
    Serial.printf("[WIFI] %s | %s\n",ssid.c_str(),ap ? "ready" : "FAILED");
    storageMgr.begin(); loraMesh.begin();
    Serial.printf("[HTTPS] %s | https://192.168.4.1/\n",web.begin() ? "ready" : "FAILED");
    Serial.printf("[STACK] fix=1 | loop=%u bytes | minimum_free=%u bytes\n",
        unsigned(LOOP_TASK_STACK_BYTES),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    Serial.println("[DELIVERY] Pending means saved locally; received means stored by the destination node. Neither means rescued.");
}
void loop() {
    web.update();
    static uint32_t lastHealth=0;
    if(millis()-lastHealth>=30000) {
        lastHealth=millis();
        Serial.printf("[HEALTH] uptime_ms=%lu | stack_min=%u | heap_free=%u | heap_min=%u | wifi_clients=%u | radio=%u | storage=%u | records=%u | rx=%lu | tx=%lu | invalid=%lu | version_reject=%lu | auth_reject=%lu | sos_store_fail=%lu | mesh_gap_max_ms=%lu | web_max_ms=%lu\n",
            (unsigned long)millis(),unsigned(uxTaskGetStackHighWaterMark(nullptr)),
            unsigned(ESP.getFreeHeap()),unsigned(ESP.getMinFreeHeap()),unsigned(WiFi.softAPgetStationNum()),
            unsigned(loraMesh.ready()),unsigned(storageMgr.ready()),unsigned(storageMgr.count()),
            (unsigned long)loraMesh.rxCount(),(unsigned long)loraMesh.txCount(),(unsigned long)loraMesh.invalidCount(),
            (unsigned long)loraMesh.versionRejectCount(),(unsigned long)loraMesh.authRejectCount(),(unsigned long)loraMesh.sosStoreFailureCount(),
            (unsigned long)loraMesh.maxServiceGapMs(),(unsigned long)web.maxLoopMs());
        Serial.printf("[RADIO_HEALTH] rx_raw=%lu rx_read_errors=%lu rx_length_errors=%lu rx_start_errors=%lu polled_events=%lu last_rx_error=%d\n",
            (unsigned long)loraMesh.rawRxCount(),(unsigned long)loraMesh.rxReadErrorCount(),
            (unsigned long)loraMesh.rxLengthErrorCount(),(unsigned long)loraMesh.rxStartErrorCount(),
            (unsigned long)loraMesh.polledEventCount(),loraMesh.lastRxError());
    }
    delay(1);
}
