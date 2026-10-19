#pragma once
#include <ArduinoJson.h>
#include <HTTPServer.hpp>
#include <HTTPSServer.hpp>
#include <SSLCert.hpp>

#include "cert_der.h"
#include "key_der.h"
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <ResourceNode.hpp>
#include <ResourceParameters.hpp>
#include <DNSServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "LoRaMeshManager.h"
#include "WebAssets.h"
#include "BufferedOutput.h"
#include "PortalRouting.h"
using namespace httpsserver;

class WebServerManager {
    StorageManager& store;
    LoRaMeshManager& mesh;
    SSLCert certificate;
    HTTPSServer server;
    HTTPServer redirectServer;
    DNSServer dns;
    // Handlers run sequentially in loopTask; mesh.update() never re-enters the
    // web server. Share fixed scratch space instead of stacking JSON documents
    // and a 1 KB body buffer above the radio/filesystem call chain.
    StaticJsonDocument<1536> requestDocument;
    char requestBody[1025];
    uint8_t responseScratch[512];
    uint32_t webLoopMax=0, lastSlowWarning=0;
    static WebServerManager* instance;
    static void route(HTTPRequest* req,HTTPResponse* res) { instance->handle(req,res); }
    static void redirect(HTTPRequest* req,HTTPResponse* res) {
        req->discardRequestBody(); res->setStatusCode(302); res->setStatusText("Found");
        res->setHeader("Location",PORTAL_URL); res->setHeader("Content-Type","text/html; charset=utf-8");
        res->println(
    "<!doctype html><html><body>"
    "<a href=\"https://192.168.4.1/\">"
    "Open secure emergency portal"
    "</a></body></html>"
);
    }
    static String query(HTTPRequest* req,const char* name) { std::string value; req->getParams()->getQueryParameter(name,value); return String(value.c_str()); }
    static void error(HTTPResponse* res,int code,const char* message) {
        res->setStatusCode(code); res->setStatusText(code==401 ? "Unauthorized" : "Request failed");
        StaticJsonDocument<256> doc; doc["error"]=message; serializeJson(doc,*res);
    }
    static bool sessionValid(const String& s) { if(s.length()!=32) return false; for(char c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false; return true; }
    bool authorised(HTTPRequest* req,HTTPResponse* res,String& session) {
        session=String(req->getHeader("X-Session").c_str());
        if(!sessionValid(session)) { error(res,400,"Missing browser session. Reload this page."); return false; }
        if(CURRENT_NODE_ROLE==ROLE_RESCUER && (req->getBasicAuthUser()!=RESCUER_USERNAME || req->getBasicAuthPassword()!=RESCUER_PASSWORD)) {
            res->setHeader("WWW-Authenticate","Basic realm=\"Heltec rescuer\""); error(res,401,"Rescuer login required. Credentials are in NetworkSecrets.h."); return false;
        }
        // API mutations require a custom header and same-origin requests; no CORS permissions are sent.
        std::string origin=req->getHeader("Origin");
        if(!origin.empty() && origin!="https://192.168.4.1") { error(res,403,"Use https://192.168.4.1/ on this Wi-Fi network."); return false; }
        return true;
    }
    bool access(const Record& r,const String& session) {
        if(CURRENT_NODE_ROLE==ROLE_RESCUER) return true;
        Record report;
        if(r.kind==SOS) return r.source==g_nodeId && session==r.owner;
        return store.get(SOS,r.link,report) && report.source==g_nodeId && session==report.owner;
    }
    bool json(HTTPRequest* req,HTTPResponse* res,JsonDocument& doc) {
        size_t n=req->getContentLength();
        if(!n || n>1024 || req->getHeader("Content-Type").find("application/json")!=0) { error(res,400,"Expected JSON under 1024 bytes."); return false; }
        char* body=requestBody; size_t got=0; uint32_t start=millis();
        while(got<n && millis()-start<5000) {
            size_t read=req->readChars(body+got,n-got); got+=read; mesh.update(); if(!read) delay(1);
        }
        body[got]=0;
        if(got!=n || deserializeJson(doc,(const char*)body,got)) { error(res,400,"Incomplete or invalid JSON."); return false; }
        return true;
    }
    template<size_t N> static bool copyText(JsonVariantConst v,char (&to)[N]) {
        if(!v.is<const char*>()) return false;
        const char* text=v.as<const char*>(); size_t n=strlen(text);
        if(n>=N) return false; memcpy(to,text,n+1); return true;
    }
    void saved(HTTPResponse* res,const Record& r) {
        mesh.recordQueued(r);
        StaticJsonDocument<192> doc; doc["id"]=idString(r.id); doc["status"]="saved_pending";
        res->setStatusCode(201); res->setStatusText("Created"); serializeJson(doc,*res);
    }
    void state(HTTPResponse* res,const String& session) {
        BufferedOutput<HTTPResponse> output(*res,responseScratch,sizeof(responseScratch));
        const char* prefix="{\"records\":[";
        output.write((const uint8_t*)prefix,strlen(prefix)); bool first=true;
        for(size_t i=0;i<store.count();i++) {
            Record r; if(!store.at(i,r) || !access(r,session)) continue;
            if(!first) output.write(','); first=false;
            requestDocument.clear(); auto& doc=requestDocument;
            doc["id"]=idString(r.id); doc["source"]=idString(r.source); doc["target"]=idString(r.target);
            doc["link"]=idString(r.link); doc["kind"]=r.kind; doc["order"]=r.order;
            doc["mine"]=r.source==g_nodeId; doc["delivery"]=r.delivery==DELIVERY_PENDING ? "pending" : r.delivery==DELIVERY_RELAY ? "relay" : "received";
            if(r.kind==SOS) {
                SosPayload p; memcpy(&p,r.payload,sizeof(p)); doc["name"]=p.name; doc["location"]=p.location; doc["text"]=p.text;
                doc["people"]=p.people; doc["flags"]=p.flags; doc["resolved"]=store.resolved(r.id); doc["timestamp"]=p.timestamp;
                doc["gps"]=bool(p.gps);
                if(p.gps) { doc["lat"]=p.latitude; doc["lon"]=p.longitude; }
                else { doc["lat"]=nullptr; doc["lon"]=nullptr; }
            } else if(r.kind==CHAT) { ChatPayload p; memcpy(&p,r.payload,sizeof(p)); doc["text"]=p.text; doc["timestamp"]=p.timestamp; }
            else if(r.kind==IMAGE) {
                ImagePayload p; memcpy(&p,r.payload,sizeof(p)); doc["bytes"]=p.bytes; doc["chunks"]=imageChunks(p.bytes);
                doc["done"]=__builtin_popcountll(r.bitmap); doc["complete"]=bool(r.complete);
            }
            serializeJson(doc,output);
            if(!output.flush()) { res->error(); return; }
            mesh.update();
        }
        output.write((const uint8_t*)"]}",2);
        if(!output.flush()) res->error();
    }
    void submitSOS(HTTPRequest* req,HTTPResponse* res,const String& session) {
        if(CURRENT_NODE_ROLE!=ROLE_CIVILIAN) { error(res,403,"Send SOS from a civilian node."); return; }
        requestDocument.clear(); auto& doc=requestDocument; if(!json(req,res,doc)) return;
        Record r; r.kind=SOS; r.source=g_nodeId; r.length=sizeof(SosPayload); session.toCharArray(r.owner,sizeof(r.owner));
        // The browser supplies an idempotency ID so a lost HTTP response does not create a second SOS.
        if(!parseId(String(doc["id"] | ""),r.id)) { error(res,400,"Invalid report ID."); return; }
        SosPayload p{}; int people=doc["people"] | 0, flags=doc["flags"] | 0;
        if(people<1 || people>99 || flags<0 || flags>31 || !copyText(doc["name"],p.name) || !copyText(doc["location"],p.location) || !copyText(doc["text"],p.text)) {
            error(res,400,"Check people (1-99) and text byte limits: name 31, location 47, message 95."); return;
        }
        p.people=people; p.flags=flags; p.timestamp=doc["timestamp"] | 0U;
        if(!doc["lat"].isNull() || !doc["lon"].isNull()) {
            if(!doc["lat"].is<double>() || !doc["lon"].is<double>()) { error(res,400,"Invalid GPS coordinates."); return; }
            double lat=doc["lat"],lon=doc["lon"];
            if(!isfinite(lat)||!isfinite(lon)||fabs(lat)>90||fabs(lon)>180) { error(res,400,"Invalid GPS coordinates."); return; }
            p.gps=1; p.latitude=lat; p.longitude=lon;
        }
        memcpy(r.payload,&p,sizeof(p)); Record existing;
        if(store.get(SOS,r.id,existing)) {
            if(!access(existing,session) || existing.length!=r.length || memcmp(existing.payload,r.payload,r.length)) error(res,409,"Report ID already used. Refresh to start another report.");
            else saved(res,existing);
            return;
        }
        if(!store.save(r)) { error(res,507,"Report was NOT saved: storage unavailable or full."); return; }
        saved(res,r);
    }
    void chatOrResolve(HTTPRequest* req,HTTPResponse* res,const String& session,bool resolve) {
        if(resolve && CURRENT_NODE_ROLE!=ROLE_RESCUER) { error(res,403,"Only a rescuer can resolve a report."); return; }
        requestDocument.clear(); auto& doc=requestDocument; if(!json(req,res,doc)) return;
        uint64_t id,reportId; Record report;
        if(!parseId(String(doc["report"]|""),reportId) || !parseId(String(doc["id"]|""),id) || !store.get(SOS,reportId,report) || !access(report,session)) {
            error(res,404,"Report not available in this browser session."); return;
        }
        if(!resolve && store.resolved(reportId)) { error(res,409,"This report has been resolved."); return; }
        Record r; r.kind=resolve ? RESOLVE : CHAT; r.id=id; r.link=reportId; r.source=g_nodeId;
        r.target=report.source==g_nodeId ? report.target : report.source; session.toCharArray(r.owner,sizeof(r.owner));
        if(resolve) { ResolvePayload p{reportId,doc["timestamp"]|0U}; r.length=sizeof(p); memcpy(r.payload,&p,sizeof(p)); }
        else { ChatPayload p{}; p.report=reportId; p.timestamp=doc["timestamp"]|0U;
            if(!copyText(doc["text"],p.text) || !p.text[0]) { error(res,400,"Chat must contain 1-159 UTF-8 bytes."); return; }
            r.length=sizeof(p); memcpy(r.payload,&p,sizeof(p));
        }
        Record existing;
        if(store.get(r.kind,id,existing)) {
            if(existing.source!=g_nodeId || existing.link!=reportId || existing.length!=r.length || memcmp(existing.payload,r.payload,r.length)) error(res,409,"Message ID conflict."); else saved(res,existing);
            return;
        }
        if(!store.save(r)) { error(res,507,"Message was NOT saved: storage unavailable or full."); return; }
        saved(res,r);
    }
    void photo(HTTPRequest* req,HTTPResponse* res,const String& session,bool upload) {
        uint64_t id; Record report;
        if(!parseId(query(req,"id"),id) || !store.get(SOS,id,report) || !access(report,session)) { error(res,404,"Report not found."); return; }
        Record image;
        if(!upload) {
            if(!store.get(IMAGE,id,image) || !image.complete) { error(res,404,"Photo is not complete yet."); return; }
            File f=LittleFS.open(store.imagePath(id),"r"); if(!f) { error(res,404,"Photo file unavailable."); return; }
            res->setHeader("Content-Type","image/jpeg"); res->setHeader("Content-Length",std::to_string(f.size()));
            uint8_t buf[512]; while(f.available()) { size_t n=f.read(buf,sizeof(buf)); if(!n) break; res->write(buf,n); mesh.update(); } f.close(); return;
        }
        if(CURRENT_NODE_ROLE!=ROLE_CIVILIAN || report.source!=g_nodeId) { error(res,403,"Only the report owner can attach a photo."); return; }
        if(store.get(IMAGE,id,image)) { saved(res,image); return; } // One immutable photo per report; retry is idempotent.
        size_t length=req->getContentLength();
        if(length<4 || length>MAX_IMAGE_BYTES || req->getHeader("Content-Type")!="image/jpeg") { error(res,413,"Use a JPEG of at most 8192 bytes."); return; }
        if(store.count()>=MAX_RECORDS || store.imageCount()>=MAX_IMAGES || LittleFS.totalBytes()-LittleFS.usedBytes()<length+20000) { error(res,507,"No space for a photo; the text report is still saved."); return; }
        String tmp=store.imagePath(id,false); File f=LittleFS.open(tmp,"w"); if(!f) { error(res,507,"Cannot store photo."); return; }
        uint8_t buf[512]; size_t count=0; uint32_t crc=0xffffffffU,start=millis(); bool ok=true;
        while(count<length && millis()-start<10000) {
            size_t wanted=length-count<sizeof(buf) ? length-count : sizeof(buf);
            size_t n=req->readBytes(buf,wanted);
            if(n) { if(f.write(buf,n)!=n) { ok=false; break; } crc=crcUpdate(crc,buf,n); count+=n; }
            mesh.update(); if(!n) delay(1);
        }
        f.flush(); f.close(); ImagePayload info{id,(uint32_t)length,~crc};
        if(!ok || count!=length || !store.verifyImage(id,info,false)) { error(res,400,"Photo upload incomplete or invalid. Retry the attachment."); return; }
        if(!LittleFS.rename(tmp,store.imagePath(id))) { error(res,507,"Cannot commit photo file."); return; }
        image.kind=IMAGE; image.id=id; image.source=g_nodeId; image.target=report.target; image.link=id; image.complete=1;
        image.length=sizeof(info); memcpy(image.payload,&info,sizeof(info)); session.toCharArray(image.owner,sizeof(image.owner));
        if(!store.save(image)) { error(res,507,"Photo metadata was not saved. Retry the attachment."); return; }
        saved(res,image);
    }
    void clearData(HTTPRequest* req,HTTPResponse* res) {
        requestDocument.clear(); auto& doc=requestDocument;
        if(!json(req,res,doc)) return;
        if(strcmp(doc["confirm"] | "","CLEAR_ALL")!=0 ||
           idString(g_nodeId)!=String(doc["node"] | "")) {
            error(res,400,"Confirm Clear all for the connected node."); return;
        }
        if(!mesh.clearStoredData()) {
            error(res,503,"Clear did not finish. Some data may already be removed. Retry Clear all on this node."); return;
        }
        res->print("{\"ok\":true,\"radio\":");
        res->print(mesh.ready() ? "true}" : "false}");
    }
    void handle(HTTPRequest* req,HTTPResponse* res) {
        String path(req->getRequestString().c_str()); int question=path.indexOf('?'); if(question>=0) path=path.substring(0,question);
        res->setHeader("Cache-Control","no-store"); res->setHeader("X-Content-Type-Options","nosniff");
        res->setHeader("Content-Security-Policy","default-src 'self'; img-src 'self' blob:; style-src 'self'; script-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
        std::string host=req->getHeader("Host");
        if(redirectToPortal(req->getMethod().c_str(),host.c_str(),path.c_str())) { redirect(req,res); return; }
        if(!isPortalHost(host.c_str())) { res->setHeader("Content-Type","application/json"); error(res,403,"Use http://192.168.4.1/."); req->discardRequestBody(); return; }
        if((req->getMethod()=="GET" || req->getMethod()=="HEAD") && (path=="/" || path=="/app.js" || path=="/style.css")) {
            res->setHeader("Content-Type",path=="/" ? "text/html; charset=utf-8" : path=="/app.js" ? "application/javascript; charset=utf-8" : "text/css; charset=utf-8");
            const char* asset=path=="/" ? WEB_INDEX : path=="/app.js" ? WEB_APP : WEB_STYLE;
            res->setHeader("Content-Length",std::to_string(strlen(asset)));
            if(req->getMethod()=="GET") res->write((const uint8_t*)asset,strlen(asset)); return;
        }
        res->setHeader("Content-Type","application/json");
        if(path=="/api/status" && req->getMethod()=="GET") {
            requestDocument.clear(); auto& doc=requestDocument; doc["node"]=idString(g_nodeId); doc["role"]=CURRENT_NODE_ROLE==ROLE_RESCUER ? "rescuer" : "civilian";
            doc["transferMode"]=TRANSFER_MODE; doc["transferName"]=transferModeName(); doc["protocol"]=MESH_VERSION;
            doc["radio"]=mesh.ready(); doc["storage"]=store.ready(); doc["records"]=store.count(); doc["limit"]=MAX_RECORDS; doc["recoverable"]=store.unclaimed();
            doc["rx"]=mesh.rxCount(); doc["tx"]=mesh.txCount(); doc["invalid"]=mesh.invalidCount(); doc["rssi"]=mesh.lastRssi(); doc["freeHeap"]=ESP.getFreeHeap();
            doc["stackFix"]=1; doc["uptimeMs"]=millis(); doc["stackBytes"]=LOOP_TASK_STACK_BYTES;
            doc["stackMinFree"]=uxTaskGetStackHighWaterMark(nullptr); doc["heapMinFree"]=ESP.getMinFreeHeap();
            doc["wifiClients"]=WiFi.softAPgetStationNum();
            doc["canClear"]=store.canClear();
            doc["firmware"]=FIRMWARE_REVISION;
            doc["versionRejects"]=mesh.versionRejectCount(); doc["authRejects"]=mesh.authRejectCount();
            doc["sosStoreFailures"]=mesh.sosStoreFailureCount();
            doc["meshGapMaxMs"]=mesh.maxServiceGapMs(); doc["webLoopMaxMs"]=webLoopMax;
            doc["rxRaw"]=mesh.rawRxCount(); doc["rxReadErrors"]=mesh.rxReadErrorCount();
            doc["rxLengthErrors"]=mesh.rxLengthErrorCount(); doc["rxStartErrors"]=mesh.rxStartErrorCount();
            doc["irqPolled"]=mesh.polledEventCount(); doc["lastRxError"]=mesh.lastRxError();
            serializeJson(doc,*res); return;
        }
        String session;
        if(!authorised(req,res,session)) { req->discardRequestBody(); return; }
        // Allow a failed clear to be retried even when normal storage is blocked.
        if(req->getMethod()=="POST" && path=="/api/clear") {
            clearData(req,res); req->discardRequestBody(); return;
        }
        if(!store.ready()) { error(res,503,"Storage unavailable; see Serial Monitor and README."); req->discardRequestBody(); return; }
        bool post=req->getMethod()=="POST",get=req->getMethod()=="GET";
        if(get && path=="/api/state") state(res,session);
        else if(post && path=="/api/sos") submitSOS(req,res,session);
        else if(post && path=="/api/chat") chatOrResolve(req,res,session,false);
        else if(post && path=="/api/resolve") chatOrResolve(req,res,session,true);
        else if((get || post) && path=="/api/image") photo(req,res,session,post);
        else if(post && path=="/api/recover" && CURRENT_NODE_ROLE==ROLE_CIVILIAN) {
            if(store.recoverFor(session)) res->print("{\"ok\":true}"); else error(res,507,"Could not claim all recovered reports. Retry when storage is available.");
        }
        else if(post && path=="/api/retry") { mesh.retryNow(); res->print("{\"ok\":true}"); }
        else if(post && path=="/api/beacon" && CURRENT_NODE_ROLE==ROLE_RESCUER) { mesh.beacon(); res->print("{\"ok\":true}"); }
        else error(res,404,"Endpoint not found.");
        req->discardRequestBody();
    }
public:
    WebServerManager(StorageManager& s, LoRaMeshManager& m)
    : store(s),
      mesh(m),
      certificate(
          cert_der,
          sizeof(cert_der),
          key_der,
          sizeof(key_der)
      ),
      server(&certificate, 443, 2),
      redirectServer(80, 2) {}

    bool begin() {
        instance = this;

        // Actual application is served over HTTPS.
        server.setDefaultNode(
            new ResourceNode("", "", route)
        );

        // HTTP requests are redirected to HTTPS.
        redirectServer.setDefaultNode(
            new ResourceNode("", "", redirect)
        );

        dns.setTTL(60);
        dns.setErrorReplyCode(DNSReplyCode::NoError);

        bool dnsReady = dns.start(
            53, "*", WiFi.softAPIP()
        );

        server.start();
        redirectServer.start();

        Serial.printf(
            "[PORTAL] DNS %s | HTTPS %s | HTTP redirect %s\n",
            dnsReady ? "ready" : "FAILED",
            server.isRunning() ? "ready" : "FAILED",
            redirectServer.isRunning() ? "ready" : "FAILED"
        );

        return dnsReady
            && server.isRunning()
            && redirectServer.isRunning();
    }

    uint32_t maxLoopMs() const { return webLoopMax; }
    void update() {
        mesh.update();
        dns.processNextRequest();

        const uint32_t started=millis();
        redirectServer.loop();
        server.loop();
        const uint32_t elapsed=millis()-started;
        if(elapsed>webLoopMax) webLoopMax=elapsed;
        if(elapsed>=1000 && (!lastSlowWarning || millis()-lastSlowWarning>=30000)) {
            lastSlowWarning=millis();
            Serial.printf("[WEB] service_ms=%lu; radio processing may have been delayed\n",(unsigned long)elapsed);
        }

        mesh.update();
    }
};
WebServerManager* WebServerManager::instance=nullptr;
