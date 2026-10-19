#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"
#include "PacketDefs.h"

inline String idString(uint64_t id) { char s[17]; snprintf(s, sizeof(s), "%016llx", (unsigned long long)id); return String(s); }
inline bool parseId(const String& s, uint64_t& id) {
    if (s.length()!=16) return false;
    id=0;
    for (size_t i=0;i<16;i++) {
        char c=s[i]; uint8_t d;
        if (c>='0' && c<='9') d=c-'0'; else if(c>='a' && c<='f') d=c-'a'+10; else return false;
        id=(id<<4)|d;
    }
    return id!=0;
}
inline uint64_t newId() { uint64_t id; do { id=(uint64_t(esp_random())<<32)|esp_random(); } while(!id); return id; }
#pragma pack(push, 1)
struct Record {
    uint32_t magic = 0x33524c48;
    uint8_t version = 3, kind = 0, delivery = DELIVERY_PENDING, complete = 0;
    uint64_t id = 0, source = 0, target = 0, link = 0;
    uint32_t order = 0;
    char owner[33] = {};
    uint16_t length = 0;
    uint8_t payload[MAX_PAYLOAD] = {};
    uint64_t bitmap = 0;
    uint32_t checksum = 0;
};
#pragma pack(pop)
// RAM-only scheduling hints. The on-flash Record layout stays unchanged.
struct RecordIndex { uint64_t id, source; uint8_t kind, delivery; };

class StorageManager {
    RecordIndex index[MAX_RECORDS] = {};
    size_t used = 0;
    uint32_t order = 0;
    bool mounted = false;
    bool filesystemMounted = false;
    static constexpr const char* CLEAR_MARKER = "/mesh3-clear.pending";
    String recordPath(uint8_t kind, uint64_t id) { return "/mesh3/" + String(kind) + "_" + idString(id) + ".rec"; }
    bool readFile(const String& path, Record& r) {
        File f=LittleFS.open(path,"r");
        if (!f || f.size()!=sizeof(r)) return false;
        bool ok=f.read((uint8_t*)&r,sizeof(r))==sizeof(r); f.close();
        PacketHeader h{}; h.kind=r.kind; h.objectId=r.id; h.length=r.length;
        return ok && r.magic==0x33524c48 && r.version==3 && r.id && r.source &&
            r.delivery<=DELIVERY_RELAY && r.complete<=1 && terminated(r.owner,sizeof(r.owner)) &&
            r.length<=MAX_PAYLOAD && (r.kind==SOS || r.kind==CHAT || r.kind==IMAGE || r.kind==RESOLVE) &&
            r.checksum==crc32(&r,offsetof(Record,checksum)) && validPayload(h,r.payload);
    }
public:
    bool begin() {
        used=0; order=0;
        mounted=LittleFS.begin(FORMAT_FILESYSTEM_ON_MOUNT_FAILURE);
        filesystemMounted=mounted;
        if (!mounted) { Serial.println("[STORAGE] Mount failed; reports were NOT erased. See README."); return false; }
        // Only a previously confirmed Clear all creates this marker. Finish it
        // before loading records or importing the legacy queue after a reboot.
        if(LittleFS.exists(CLEAR_MARKER) && !clearAll()) return false;
        if (!LittleFS.exists("/mesh3") && !LittleFS.mkdir("/mesh3")) { mounted=false; return false; }
        File dir=LittleFS.open("/mesh3"); File f=dir.openNextFile();
        while(f) {
            String path=f.path(); bool candidate=!f.isDirectory() && path.endsWith(".rec"); f.close();
            if(candidate) {
                Record r;
                if (readFile(path,r) && used<MAX_RECORDS) { index[used++]={r.id,r.source,r.kind,r.delivery}; if(r.order>order) order=r.order; }
                else Serial.println("[STORAGE] Unreadable/excess record retained: "+path);
            }
            f=dir.openNextFile();
        }
        dir.close();
        importLegacyPending();
        Serial.printf("[STORAGE] %u records; %u of %u bytes used\n",(unsigned)used,(unsigned)LittleFS.usedBytes(),(unsigned)LittleFS.totalBytes());
        return true;
    }
    // The previous restored sketch used 124-byte native records: 4-byte ID,
    // 110-byte packed payload, alignment padding, saved time, retry count and bool.
    // Import only unsent records. The original files remain untouched.
    void importLegacyPending() {
        if(CURRENT_NODE_ROLE!=ROLE_CIVILIAN || !mounted || !LittleFS.exists("/unsent_sos.bin")) return;
        File f=LittleFS.open("/unsent_sos.bin","r");
        if(!f || f.size()%124) { Serial.println("[STORAGE] Legacy file layout not recognised; retained without conversion."); return; }
        uint8_t raw[124]; uint32_t index=0;
        while(f.read(raw,sizeof(raw))==sizeof(raw)) {
            uint32_t oldId; memcpy(&oldId,raw,4); uint64_t seed[2]={g_nodeId,(uint64_t(oldId)<<32)|index++};
            if(raw[121]!=0) continue;
            uint64_t id=(uint64_t(crc32(raw,sizeof(raw)))<<32)|crc32(seed,sizeof(seed)); if(!id) id=1;
            Record existing; if(get(SOS,id,existing)) continue;
            SosPayload p{}; p.flags=raw[4]&31; p.people=raw[5]; if(p.people<1 || p.people>99) continue;
            memcpy(p.location,raw+6,16); p.location[16]=0;
            memcpy(p.name,raw+22,16); p.name[16]=0;
            memcpy(p.text,raw+38,64); p.text[64]=0;
            memcpy(&p.latitude,raw+106,4); memcpy(&p.longitude,raw+110,4);
            p.gps=isfinite(p.latitude)&&isfinite(p.longitude)&&fabs(p.latitude)<=90&&fabs(p.longitude)<=180&&(p.latitude!=0||p.longitude!=0);
            // The old timestamp was uptime, so it cannot be used as a calendar date.
            Record r; r.kind=SOS; r.id=id; r.source=g_nodeId; r.length=sizeof(p); memcpy(r.payload,&p,sizeof(p));
            if(!save(r)) { Serial.println("[STORAGE] No room for more legacy imports; original queue retained."); break; }
        }
        f.close();
    }
    size_t unclaimed() {
        size_t n=0; for(size_t i=0;i<used;i++) if(index[i].kind==SOS) { Record r; if(at(i,r)&&r.source==g_nodeId&&!r.owner[0]) n++; } return n;
    }
    bool recoverFor(const String& session) {
        for(size_t i=0;i<used;i++) if(index[i].kind==SOS) {
            Record r; if(at(i,r)&&r.source==g_nodeId&&!r.owner[0]) { memcpy(r.owner,session.c_str(),32); r.owner[32]=0; if(!save(r)) return false; }
        }
        return true;
    }
    bool ready() const { return mounted; }
    bool canClear() const { return filesystemMounted; }
    bool clearAll() {
        if(!filesystemMounted) return false;
        if(!LittleFS.exists(CLEAR_MARKER)) {
            File marker=LittleFS.open(CLEAR_MARKER,"w");
            if(!marker) return false;
            marker.flush(); marker.close();
            if(!LittleFS.exists(CLEAR_MARKER)) return false;
        }
        // Block saves/ACKs until deletion finishes. Keep the marker on failure
        // so retrying or rebooting completes the same clear operation.
        mounted=false; used=0; order=0;
        for(auto& item:index) item={};
        if(LittleFS.exists("/unsent_sos.bin") && !LittleFS.remove("/unsent_sos.bin")) return false;
        if(!LittleFS.exists("/mesh3") && !LittleFS.mkdir("/mesh3")) return false;
        for(;;) {
            File dir=LittleFS.open("/mesh3");
            if(!dir || !dir.isDirectory()) return false;
            File f=dir.openNextFile();
            if(!f) { dir.close(); break; }
            String path=f.path(); bool directory=f.isDirectory();
            f.close(); dir.close();
            // This directory is flat and belongs exclusively to message data.
            // Reopen after each removal instead of mutating a live iterator.
            if(directory || strncmp(path.c_str(),"/mesh3/",7)!=0 || !LittleFS.remove(path)) return false;
            delay(1);
        }
        if(!LittleFS.remove(CLEAR_MARKER)) return false;
        mounted=true;
        Serial.println("[STORAGE] All local reports, messages and photos cleared.");
        return true;
    }
    size_t count() const { return used; }
    bool needsTransmission(size_t i,uint64_t node) const {
        return i<used && ((index[i].source==node && index[i].delivery==DELIVERY_PENDING) ||
            (index[i].kind==SOS && index[i].delivery==DELIVERY_RELAY));
    }
    int find(uint8_t kind,uint64_t id) const { for(size_t i=0;i<used;i++) if(index[i].kind==kind && index[i].id==id) return i; return -1; }
    bool at(size_t i, Record& r) { return mounted && i<used && readFile(recordPath(index[i].kind,index[i].id),r); }
    bool get(uint8_t kind,uint64_t id,Record& r) { int i=find(kind,id); return i>=0 && at(i,r); }
    size_t imageCount() const { size_t n=0; for(size_t i=0;i<used;i++) if(index[i].kind==IMAGE) ++n; return n; }
    bool save(Record& r) {
        if(!mounted || r.length>MAX_PAYLOAD) return false;
        int i=find(r.kind,r.id); bool fresh=i<0;
        if(fresh && (used>=MAX_RECORDS || (r.kind==IMAGE && imageCount()>=MAX_IMAGES))) return false;
        size_t freeBytes=LittleFS.totalBytes()-LittleFS.usedBytes();
        if(freeBytes < (fresh ? 16384U : 2048U)) return false;
        if(fresh && !r.order) r.order=++order;
        r.checksum=crc32(&r,offsetof(Record,checksum));
        String path=recordPath(r.kind,r.id), tmp=path+".tmp";
        File f=LittleFS.open(tmp,"w"); if(!f) return false;
        bool ok=f.write((const uint8_t*)&r,sizeof(r))==sizeof(r); f.flush(); f.close();
        Record verify;
        ok=ok && readFile(tmp,verify) && memcmp(&verify,&r,sizeof(r))==0;
        // LittleFS rename replaces the destination atomically; never remove it first.
        if(!ok || !LittleFS.rename(tmp,path)) return false;
        if(fresh) index[used++]={r.id,r.source,r.kind,r.delivery};
        else index[i]={r.id,r.source,r.kind,r.delivery};
        return true;
    }
    bool sameContent(const Record& r, const PacketHeader& h, const uint8_t* data) const {
        return r.source==h.source && r.length==h.length && memcmp(r.payload,data,h.length)==0;
    }
    bool resolved(uint64_t report) {
        for(size_t i=0;i<used;i++) if(index[i].kind==RESOLVE) { Record r; if(at(i,r) && r.link==report) return true; }
        return false;
    }
    String imagePath(uint64_t id,bool complete=true) { return "/mesh3/"+idString(id)+(complete ? ".jpg" : ".part"); }
    bool verifyImage(uint64_t id,const ImagePayload& p,bool finished) {
        File f=LittleFS.open(imagePath(id,finished),"r");
        if(!f || f.size()!=p.bytes) return false;
        uint8_t buf[512], first[2]={}, last[2]={}; uint32_t c=0xffffffffU; size_t pos=0;
        while(f.available()) {
            size_t n=f.read(buf,sizeof(buf)); if(!n) { f.close(); return false; }
            if(!pos) { first[0]=buf[0]; first[1]=buf[1]; }
            for(size_t j=0;j<n;j++) { last[0]=last[1]; last[1]=buf[j]; }
            c=crcUpdate(c,buf,n); pos+=n;
        }
        f.close();
        return pos==p.bytes && ~c==p.crc && first[0]==0xff && first[1]==0xd8 && last[0]==0xff && last[1]==0xd9;
    }
    bool beginIncomingImage(const PacketHeader& h,const ImagePayload& p,Record& r) {
        if(get(IMAGE,h.objectId,r)) return sameContent(r,h,(const uint8_t*)&p);
        if(imageCount()>=MAX_IMAGES || used>=MAX_RECORDS || LittleFS.totalBytes()-LittleFS.usedBytes()<p.bytes+20000U) return false;
        // A dangling .part from an interrupted manifest can safely be replaced.
        File f=LittleFS.open(imagePath(h.objectId,false),"w"); if(!f) return false; f.close();
        r=Record(); r.kind=IMAGE; r.id=h.objectId; r.source=h.source; r.target=g_nodeId; r.link=p.report;
        r.delivery=DELIVERY_RECEIVED; r.length=sizeof(p); memcpy(r.payload,&p,sizeof(p));
        return save(r);
    }
    // 0 = could not persist, 1 = chunk saved, 2 = complete/verified, 3 = restart after CRC failure.
    // Used by bitmap queries, including after a reboot or a lost final ACK.
    // Only committed metadata is reported. Repair interrupted finalisation first.
    uint8_t imageReceipt(Record& r) {
        ImagePayload p; memcpy(&p,r.payload,sizeof(p));
        if(r.complete) return verifyImage(r.id,p,true) ? 2 : 0;
        if(LittleFS.exists(imagePath(r.id)) && verifyImage(r.id,p,true)) {
            r.complete=1; r.bitmap=completeMask(p.bytes); return save(r) ? 2 : 0;
        }
        if(!LittleFS.exists(imagePath(r.id,false))) {
            r.bitmap=0; if(!save(r)) return 0;
            File empty=LittleFS.open(imagePath(r.id,false),"w"); if(!empty) return 0; empty.close();
        }
        if(r.bitmap!=completeMask(p.bytes)) return 1;
        if(!verifyImage(r.id,p,false)) {
            r.bitmap=0; if(!save(r)) return 0;
            File empty=LittleFS.open(imagePath(r.id,false),"w"); if(!empty) return 0; empty.close(); return 3;
        }
        if(!LittleFS.rename(imagePath(r.id,false),imagePath(r.id))) return 0;
        r.complete=1; return save(r) ? 2 : 0;
    }
    uint8_t receiveChunk(Record& r,const ChunkPayload& chunk) {
        ImagePayload p; memcpy(&p,r.payload,sizeof(p));
        if(r.complete) return 2;
        // Recover a power interruption between final rename and saving the completion record.
        if(LittleFS.exists(imagePath(r.id)) && verifyImage(r.id,p,true)) {
            r.complete=1; r.bitmap=completeMask(p.bytes); return save(r) ? 2 : 0;
        }
        uint32_t offset=uint32_t(chunk.index)*CHUNK_BYTES;
        if(offset>=p.bytes || chunk.count!=((p.bytes-offset)<CHUNK_BYTES ? p.bytes-offset : CHUNK_BYTES)) return 0;
        if(!LittleFS.exists(imagePath(r.id,false))) {
            r.bitmap=0; if(!save(r)) return 0;
            File empty=LittleFS.open(imagePath(r.id,false),"w"); if(!empty) return 0; empty.close();
        }
        if(!(r.bitmap & (uint64_t(1)<<chunk.index))) {
            File f=LittleFS.open(imagePath(r.id,false),"r+"); if(!f) return 0;
            bool ok=f.seek(offset) && f.write(chunk.data,chunk.count)==chunk.count; f.flush(); f.close();
            if(!ok) return 0;
            // Read back before acknowledging durable storage.
            f=LittleFS.open(imagePath(r.id,false),"r"); uint8_t check[CHUNK_BYTES];
            ok=f && f.seek(offset) && f.read(check,chunk.count)==chunk.count && memcmp(check,chunk.data,chunk.count)==0; f.close();
            if(!ok) return 0;
            r.bitmap |= uint64_t(1)<<chunk.index; if(!save(r)) return 0;
        }
        if(r.bitmap!=completeMask(p.bytes)) return 1;
        if(!verifyImage(r.id,p,false)) {
            r.bitmap=0; if(!save(r)) return 0;
            File f=LittleFS.open(imagePath(r.id,false),"w"); if(!f) return 0; f.close(); return 3;
        }
        if(!LittleFS.rename(imagePath(r.id,false),imagePath(r.id))) return 0;
        r.complete=1; return save(r) ? 2 : 0;
    }
};
