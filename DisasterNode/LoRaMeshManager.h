#pragma once
#include <SPI.h>
#include <RadioLib.h>
#include <mbedtls/md.h>
#include "StorageManager.h"
#include "NetworkSecrets.h"
#include "TransferPolicy.h"
void IRAM_ATTR setLoRaRxFlag();

struct RetryState {
    uint32_t due = 0;
    uint8_t attempts = 0;
    bool manifest = false, deferred = false;
    bool timerArmed = false;
    uint16_t waiting = META_ACK;
    uint8_t pathHops = MESH_HOPS;
};
class LoRaMeshManager {
#ifdef HELTEC_TEST
    friend struct MeshTestProbe;
#endif
    struct Frame { uint8_t bytes[255]; uint16_t length=0; uint8_t priority=9; uint32_t due=0; };
    StorageManager* storage;
    SX1262* radio=nullptr;
    Frame queue[16];
    RetryState retries[MAX_RECORDS];
    GroupTransfer groups[MAX_IMAGES];
    bool startLogged[MAX_RECORDS] = {};
    Frame transmittingFrame;
    uint64_t activeImage = 0;
    uint32_t imageNotBefore = 0, urgentUntil = 0, bootId = 0;
    uint32_t receivingSince = 0;
    uint64_t seen[64]={}; uint8_t seenAt=0;
    volatile bool irq=false;
    bool active=false, transmitting=false;
    uint32_t txStarted=0, lastPlan=0, lastBeacon=0;
    size_t cursor=0;
    uint32_t receivedFrames=0, sentFrames=0, rejectedFrames=0;
    uint32_t versionRejects=0, authRejects=0, sosStoreFailures=0;
    uint32_t lastService=0, serviceGapMax=0;
    uint32_t lastIrqPoll=0, nextReceiveRetry=0;
    uint32_t rawReceiveFrames=0, receiveReadErrors=0, receiveLengthErrors=0, receiveStartErrors=0, polledEvents=0;
    int lastReceiveError=0;
    bool receiveRestartPending=false;
    float rssi=0;
    static uint8_t priority(uint8_t kind) { return (kind==ACK || kind==BITMAP_ACK) ? 0 : (kind==SOS || kind==RESOLVE) ? 1 : kind==CHAT ? 2 : kind==BEACON ? 3 : 4; }
    static bool due(uint32_t t) { return int32_t(millis()-t)>=0; }
    static bool imageFrame(uint8_t kind) { return kind==IMAGE || kind==CHUNK || kind==GROUP_CHUNK || kind==BITMAP_QUERY; }
    bool restartReceive(const char* reason) {
        // A CAD-complete callback can leave irq set. Clear that software flag
        // before rearming RX; actual RX/TX events are checked in the chip below.
        irq=false; receivingSince=0;
        const bool recovering=receiveRestartPending;
        const int result=radio->startReceive();
        receiveRestartPending=result!=RADIOLIB_ERR_NONE;
        if(receiveRestartPending) {
            lastReceiveError=result; ++receiveStartErrors;
            nextReceiveRetry=millis()+1000;
            if(receiveStartErrors<=4 || receiveStartErrors%16==0)
                Serial.printf("[RADIO] RX_START_FAILED reason=%s code=%d count=%lu; retry in 1s\n",
                    reason,result,(unsigned long)receiveStartErrors);
        } else if(recovering) Serial.println("[RADIO] RX_RECOVERED");
        return !receiveRestartPending;
    }
    void receiveError(int result,size_t bytes,uint32_t flags) {
        lastReceiveError=result; ++receiveReadErrors;
        if(receiveReadErrors<=4 || receiveReadErrors%16==0)
            Serial.printf("[RADIO] RX_READ_FAILED code=%d bytes=%u flags=0x%04lx count=%lu\n",
                result,unsigned(bytes),(unsigned long)flags,(unsigned long)receiveReadErrors);
    }
    void event(const char* name,uint8_t kind,uint64_t id,uint16_t chunk=META_ACK,uint32_t bytes=0,
               uint64_t bitmap=0,uint8_t window=0,uint32_t rto=0,uint32_t airtime=0,uint32_t crc=0) {
#if EXPERIMENT_LOGGING
        Serial.printf("[EXP],%u,%016llx,%08lx,%lu,%s,%u,%016llx,%u,%lu,%016llx,%u,%lu,%lu,%08lx\n",
            unsigned(TRANSFER_MODE),(unsigned long long)g_nodeId,(unsigned long)bootId,(unsigned long)millis(),name,
            unsigned(kind),(unsigned long long)id,unsigned(chunk),(unsigned long)bytes,(unsigned long long)bitmap,
            unsigned(window),(unsigned long)rto,(unsigned long)airtime,(unsigned long)crc);
#endif
    }
    void observe(const Record& r,bool recovered=false) {
        int i=storage->find(r.kind,r.id);
        if(i<0 || r.source!=g_nodeId || r.delivery!=DELIVERY_PENDING || startLogged[i]) return;
        startLogged[i]=true;
        uint32_t bytes=r.length,crc=crc32(r.payload,r.length);
        if(r.kind==IMAGE) { ImagePayload p; memcpy(&p,r.payload,sizeof(p)); bytes=p.bytes; crc=p.crc; }
        event(recovered ? "RESUME" : "START",r.kind,r.id,META_ACK,bytes,r.bitmap,0,0,0,crc);
    }
    GroupTransfer* group(uint64_t id) {
        for(auto& g:groups) if(g.id==id) return &g;
        for(auto& g:groups) if(!g.id) { g=GroupTransfer(); g.id=id; return &g; }
        return nullptr;
    }
    bool queuedImage(uint64_t id) const {
        for(const auto& q:queue) if(q.length) {
            PacketHeader h; memcpy(&h,q.bytes,sizeof(h));
            if(h.source==g_nodeId && h.objectId==id && imageFrame(h.kind)) return true;
        }
        return false;
    }
    void cancelQueuedImage(uint64_t id) {
        for(auto& q:queue) if(q.length) {
            PacketHeader h; memcpy(&h,q.bytes,sizeof(h));
            if(h.source==g_nodeId && h.objectId==id && imageFrame(h.kind)) q.length=0;
        }
    }
    bool urgentTraffic() const {
        if(urgentUntil && !due(urgentUntil)) return true;
        for(const auto& q:queue) if(q.length && q.priority>=1 && q.priority<=2) return true;
        return false;
    }
    void interruptRateMeasurements() {
        for(auto& g:groups) g.ignoreMeasurement();
    }
    bool tag(uint8_t* bytes,size_t n,bool verify) {
        uint8_t digest[32];
        if(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),MESH_KEY,sizeof(MESH_KEY),bytes,n,digest)!=0) return false;
        if(!verify) { memcpy(bytes+n,digest,AUTH_TAG_BYTES); return true; }
        uint8_t diff=0; for(size_t i=0;i<AUTH_TAG_BYTES;i++) diff |= digest[i]^bytes[n+i]; return diff==0;
    }
    bool remember(uint64_t id) {
        for(auto x:seen) if(x==id) return false;
        seen[seenAt++ % 64]=id; return true;
    }
    bool enqueue(uint8_t* bytes,size_t n,uint8_t prio,uint32_t delayMs=0) {
        int slot=-1;
        for(size_t i=0;i<16;i++) if(!queue[i].length) { slot=i; break; }
        if(slot<0) { // ACK/SOS/chat can displace a lower-priority frame; durable sources retry it.
            for(size_t i=0;i<16;i++) if(queue[i].priority>prio && (slot<0 || queue[i].priority>queue[slot].priority)) slot=i;
        }
        if(slot<0) return false;
        memcpy(queue[slot].bytes,bytes,n); queue[slot].length=n; queue[slot].priority=prio; queue[slot].due=millis()+delayMs;
        return true;
    }
    bool send(uint8_t kind,uint64_t id,uint64_t source,uint64_t target,const void* payload,uint16_t length,uint8_t ttl=MESH_HOPS,uint64_t* packetId=nullptr) {
        if(!active || length>MAX_PAYLOAD) return false;
        uint8_t frame[255]; PacketHeader h{};
        h.magic=MESH_MAGIC; h.version=MESH_VERSION; h.kind=kind; h.packetId=newId(); h.source=source; h.target=target;
        h.objectId=id; h.ttl=ttl; h.length=length;
        memcpy(frame,&h,sizeof(h)); if(length) memcpy(frame+sizeof(h),payload,length);
        if(!tag(frame,sizeof(h)+length,false)) return false;
        remember(h.packetId);
        const uint32_t jitter=(TRANSFER_MODE!=TRANSFER_BASELINE && (imageFrame(kind) || kind==BITMAP_ACK ||
            (kind==ACK && length==sizeof(AckPayload) && ((const AckPayload*)payload)->kind==IMAGE))) ? 40 : 250;
        bool queued=enqueue(frame,sizeof(h)+length+AUTH_TAG_BYTES,priority(kind),esp_random()%jitter);
        if(queued && packetId) *packetId=h.packetId;
        return queued;
    }
    bool acknowledge(const PacketHeader& h,uint8_t kind,uint16_t chunk=META_ACK,uint8_t flags=0) {
        AckPayload p{kind,chunk,flags}; return send(ACK,h.objectId,g_nodeId,h.source,&p,sizeof(p));
    }
    void acceptAck(const PacketHeader& h,const AckPayload& p) {
        Record r; int i=storage->find(p.kind,h.objectId);
        if(i<0 || !storage->at(i,r) || r.source!=g_nodeId || h.target!=g_nodeId || r.delivery!=DELIVERY_PENDING) return;
        if(r.target && r.target!=h.source) return;
        if(p.kind!=SOS && !r.target) return;
        RetryState& state=retries[i];
        if(p.kind==IMAGE) {
            if(GROUPED_TRANSFER) return; // Grouped transfers use correlated bitmap replies.
            if(TRANSFER_MODE==TRANSFER_TUNED) state.pathHops=ackPathHops(h.ttl);
            ImagePayload info; memcpy(&info,r.payload,sizeof(info));
            if(p.flags & ACK_RESTART) {
                r.bitmap=0;
                if(storage->save(r)) { state.manifest=false; state.due=millis()+1000; state.timerArmed=false; }
                return;
            }
            if(p.flags & ACK_COMPLETE) {
                r.bitmap=completeMask(info.bytes); r.delivery=DELIVERY_RECEIVED;
            } else if(p.chunk==META_ACK) {
                if(state.manifest) return; // A delayed manifest ACK must not interrupt a chunk wait.
                // The receiver may have reset its bitmap after a CRC error whose restart ACK was lost.
                // All chunk ACKs without a completion ACK are insufficient: start a fresh pass.
                if(r.bitmap==completeMask(info.bytes)) { r.bitmap=0; if(!storage->save(r)) return; }
                state.manifest=true; state.due=millis()+(TRANSFER_MODE==TRANSFER_BASELINE ? 300 : 30); state.attempts=0; state.deferred=false; state.timerArmed=false; return;
            } else {
                // Only the chunk currently in flight can advance this sender.
                if(!state.manifest || p.chunk!=state.waiting || p.chunk>=imageChunks(info.bytes)) return;
                r.bitmap |= uint64_t(1)<<p.chunk;
            }
        } else { if(p.chunk!=META_ACK || p.flags) return; r.delivery=DELIVERY_RECEIVED; r.target=h.source; }
        if(storage->save(r)) {
            state.attempts=0; state.deferred=false; state.timerArmed=false;
            state.due=millis()+(p.kind==IMAGE && TRANSFER_MODE==TRANSFER_TUNED ? 30 : 300);
            if(p.kind==IMAGE && TRANSFER_MODE==TRANSFER_TUNED) cancelQueuedImage(r.id);
            event(r.delivery==DELIVERY_RECEIVED ? "DELIVERED" : "PROGRESS",r.kind,r.id,p.chunk,0,r.bitmap);
        }
    }
    void bitmapReply(const PacketHeader& h,Record& image) {
        uint8_t result=storage->imageReceipt(image);
        if(!result) return;
        BitmapAckPayload p{h.packetId,image.bitmap,uint8_t(result==2 ? ACK_COMPLETE : 0)};
        send(BITMAP_ACK,h.objectId,g_nodeId,h.source,&p,sizeof(p));
        event("RECEIPT",IMAGE,h.objectId,META_ACK,0,image.bitmap);
    }
    uint32_t groupWait(const GroupTransfer& g,bool manifest) const {
        const size_t requestBytes=sizeof(PacketHeader)+AUTH_TAG_BYTES+(manifest ? sizeof(ImagePayload) : 0);
        uint32_t floor=fixedAckWaitMs(g.pathHops,requestBytes,sizeof(PacketHeader)+sizeof(BitmapAckPayload)+AUTH_TAG_BYTES);
        return TRANSFER_MODE==TRANSFER_ADAPTIVE && g.rto>floor ? g.rto : floor;
    }
    void acceptBitmap(const PacketHeader& h,const BitmapAckPayload& ack) {
        Record r; int i=storage->find(IMAGE,h.objectId);
        if(i<0 || !storage->at(i,r) || r.source!=g_nodeId || r.target!=h.source || h.target!=g_nodeId || r.delivery!=DELIVERY_PENDING) return;
        GroupTransfer* g=group(r.id);
        if(!g || !g->awaiting || ack.request!=g->request) return;
        ImagePayload info; memcpy(&info,r.payload,sizeof(info)); uint64_t all=completeMask(info.bytes);
        if((ack.bitmap & ~all) || ((ack.flags & ACK_COMPLETE) && ack.bitmap!=all)) return;
        const uint64_t previousBitmap=r.bitmap;
        // This snapshot is authoritative: a receiver may have reset after CRC failure.
        r.bitmap=ack.bitmap;
        if(ack.flags & ACK_COMPLETE) r.delivery=DELIVERY_RECEIVED;
        if(!storage->save(r)) return;
        const bool wasManifest=g->needManifest;
        const bool clean=(g->batchMask & ack.bitmap)==g->batchMask;
        const uint8_t oldPath=g->pathHops;
        g->pathHops=ackPathHops(h.ttl);
        uint32_t floor=fixedAckWaitMs(g->pathHops,sizeof(PacketHeader)+AUTH_TAG_BYTES,
            sizeof(PacketHeader)+sizeof(BitmapAckPayload)+AUTH_TAG_BYTES);
        // Manifest RTT establishes timing only; an ACK by itself is not a speed sample.
        g->feedback(millis()-g->requestEnded,floor);
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
        if(!wasManifest && g->measuring) {
            const uint32_t now=millis(),elapsed=now-g->groupStarted;
            const uint32_t bytes=bitmapDataBytes(ack.bitmap & g->batchMask & ~previousBitmap,info.bytes);
            // A CRC reset or route change invalidates this comparison. The next
            // group measures the new conditions using the durable bitmap.
            if((previousBitmap & ~ack.bitmap) || oldPath!=g->pathHops) {
                g->ignoreMeasurement(); g->ratePolicy.restart();
            }
            const bool used=g->finishMeasurement(now,bytes,!clean,
                GroupTransfer::groupCap(frameAirtimeMs(sizeof(PacketHeader)+sizeof(ChunkPayload)+AUTH_TAG_BYTES)));
#if EXPERIMENT_LOGGING
            Serial.printf("[ADAPT] id=%016llx | chunks=%u | confirmed_bytes=%lu | elapsed_ms=%lu | bytes_per_s=%lu | next=%u | missing=%u | ack_timeouts=%u | used_for_policy=%u\n",
                (unsigned long long)r.id,unsigned(g->batchWindow),(unsigned long)bytes,(unsigned long)elapsed,
                (unsigned long)(elapsed ? uint64_t(bytes)*1000/elapsed : 0),unsigned(g->window),unsigned(!clean),
                unsigned(g->unanswered),unsigned(used));
#endif
        }
        g->unanswered=0;
#endif
        g->endMeasurement();
        g->needManifest=false; g->awaiting=false; g->request=0; g->batchMask=0; g->remaining=0;
        retries[i].attempts=0; retries[i].deferred=false; retries[i].due=millis()+30;
        cancelQueuedImage(r.id); activeImage=0;
        event(r.delivery==DELIVERY_RECEIVED ? "DELIVERED" : "PROGRESS",IMAGE,r.id,META_ACK,0,r.bitmap,g->window,g->rto);
    }
    bool storeMessage(const PacketHeader& h,const uint8_t* payload,uint64_t link,uint8_t delivery) {
        Record r;
        if(storage->get(h.kind,h.objectId,r)) return storage->sameContent(r,h,payload);
        r.kind=h.kind; r.id=h.objectId; r.source=h.source; r.target=h.target; r.link=link;
        r.delivery=delivery; r.length=h.length; memcpy(r.payload,payload,h.length);
        if(h.kind==SOS && CURRENT_NODE_ROLE==ROLE_RESCUER) r.target=g_nodeId;
        return storage->save(r);
    }
    bool validConversation(const PacketHeader& h,uint64_t reportId) {
        Record report; if(!storage->get(SOS,reportId,report)) return false;
        if(CURRENT_NODE_ROLE==ROLE_RESCUER) return report.source==h.source && h.target==g_nodeId;
        if(report.source!=g_nodeId || (report.target && report.target!=h.source)) return false;
        if(!report.target) { report.target=h.source; if(!storage->save(report)) return false; }
        return h.target==g_nodeId;
    }
    void incoming(uint8_t* bytes,size_t n) {
        PacketHeader h{};
        if(!parseFrame(bytes,n,h)) {
            rejectedFrames++;
            if(h.magic==MESH_MAGIC && h.version!=MESH_VERSION) {
                ++versionRejects;
                if(versionRejects==1 || versionRejects%16==0)
                    Serial.printf("[RADIO] RX_REJECT version=%u expected=%u count=%lu\n",
                        unsigned(h.version),unsigned(MESH_VERSION),(unsigned long)versionRejects);
            } else if(rejectedFrames==1 || rejectedFrames%16==0)
                Serial.printf("[RADIO] RX_REJECT invalid_frame bytes=%u count=%lu\n",unsigned(n),(unsigned long)rejectedFrames);
            return;
        }
        if(!tag(bytes,n-AUTH_TAG_BYTES,true)) {
            rejectedFrames++; ++authRejects;
            if(authRejects==1 || authRejects%16==0)
                Serial.printf("[RADIO] RX_REJECT authentication count=%lu; check shared mesh key\n",(unsigned long)authRejects);
            return;
        }
        if(!remember(h.packetId)) return;
        receivedFrames++;
        const uint8_t* p=bytes+sizeof(h);
        if(h.source==g_nodeId) return;
        if(h.kind==SOS)
            Serial.printf("[RADIO] RX_SOS id=%016llx source=%016llx target=%016llx role=%s\n",
                (unsigned long long)h.objectId,(unsigned long long)h.source,(unsigned long long)h.target,
                CURRENT_NODE_ROLE==ROLE_RESCUER ? "rescuer" : "civilian");
        if(h.kind==SOS || h.kind==RESOLVE) urgentUntil=millis()+URGENT_HOLD_MS;
        if(h.kind==SOS || h.kind==RESOLVE || h.kind==CHAT) interruptRateMeasurements();
        if(h.kind==BEACON) {
            // Wake pending data and durable SOS relay copies, with jitter to avoid a simultaneous flood.
            for(size_t i=0;i<storage->count();i++) {
                if(retries[i].deferred || retries[i].due==0) {
                    retries[i].due=millis()+1000+esp_random()%5000; retries[i].attempts=0; retries[i].deferred=false;
                }
            }
        }
        // Stop periodic custody retransmission once an ACK for this SOS crosses a relay.
        if(h.kind==ACK) {
            AckPayload a; memcpy(&a,p,sizeof(a)); Record held;
            if(a.kind==SOS && a.chunk==META_ACK && !a.flags && storage->get(SOS,h.objectId,held) && held.delivery==DELIVERY_RELAY && held.source==h.target) {
                held.delivery=DELIVERY_RECEIVED; held.target=h.source; storage->save(held);
            }
        }
        if(h.kind==SOS && (h.target==0 || h.target==g_nodeId)) {
            bool saved=storeMessage(h,p,0,CURRENT_NODE_ROLE==ROLE_RESCUER ? DELIVERY_RECEIVED : DELIVERY_RELAY);
            if(saved && CURRENT_NODE_ROLE==ROLE_RESCUER) {
                bool queued=acknowledge(h,SOS);
                Serial.printf("[RADIO] SOS_%s id=%016llx\n",queued ? "ACK_QUEUED" : "ACK_QUEUE_FAILED",(unsigned long long)h.objectId);
            } else if(!saved) {
                ++sosStoreFailures;
                Serial.printf("[RADIO] SOS_STORE_FAILED id=%016llx storage=%u records=%u/%u; save failed or content conflict\n",
                    (unsigned long long)h.objectId,unsigned(storage->ready()),unsigned(storage->count()),unsigned(MAX_RECORDS));
            } else Serial.println("[RADIO] SOS_RELAY_ONLY; this board is configured as civilian");
        }
        if(h.target==g_nodeId) {
            if(h.kind==ACK) { AckPayload a; memcpy(&a,p,sizeof(a)); acceptAck(h,a); }
            if(h.kind==BITMAP_ACK) { BitmapAckPayload a; memcpy(&a,p,sizeof(a)); acceptBitmap(h,a); }
            if(h.kind==CHAT) {
                ChatPayload c; memcpy(&c,p,sizeof(c));
                if(validConversation(h,c.report) && storeMessage(h,p,c.report,DELIVERY_RECEIVED)) acknowledge(h,CHAT);
            }
            if(h.kind==RESOLVE) {
                ResolvePayload c; memcpy(&c,p,sizeof(c));
                if(CURRENT_NODE_ROLE==ROLE_CIVILIAN && validConversation(h,c.report) && storeMessage(h,p,c.report,DELIVERY_RECEIVED)) acknowledge(h,RESOLVE);
            }
            if(h.kind==IMAGE && CURRENT_NODE_ROLE==ROLE_RESCUER) {
                ImagePayload c; memcpy(&c,p,sizeof(c)); Record report,image;
                if(storage->get(SOS,c.report,report) && report.source==h.source && storage->beginIncomingImage(h,c,image)) {
                    if(GROUPED_TRANSFER) bitmapReply(h,image);
                    else acknowledge(h,IMAGE,META_ACK,image.complete ? ACK_COMPLETE : 0);
                }
            }
            if((h.kind==CHUNK || h.kind==GROUP_CHUNK) && CURRENT_NODE_ROLE==ROLE_RESCUER) {
                Record image;
                if(storage->get(IMAGE,h.objectId,image) && image.source==h.source) {
                    ChunkPayload chunk{}; memcpy(&chunk,p,h.length);
                    uint8_t result=storage->receiveChunk(image,chunk);
                    if(result && h.kind==CHUNK) acknowledge(h,IMAGE,chunk.index,result==2 ? ACK_COMPLETE : result==3 ? ACK_RESTART : 0);
                }
            }
            if(h.kind==BITMAP_QUERY && CURRENT_NODE_ROLE==ROLE_RESCUER) {
                Record image;
                if(storage->get(IMAGE,h.objectId,image) && image.source==h.source) bitmapReply(h,image);
                // No manifest on disk: silence causes a bounded query retry then a fresh manifest.
            }
        }
        if(h.ttl>1 && h.target!=g_nodeId) {
            h.ttl--; memcpy(bytes,&h,sizeof(h)); tag(bytes,n-AUTH_TAG_BYTES,false);
            enqueue(bytes,n,priority(h.kind),300+esp_random()%700);
        }
    }
    void planGroup(Record& image,int index) {
        GroupTransfer* g=group(image.id); if(!g) return;
        RetryState& state=retries[index];
        if(queuedImage(image.id)) return;
        activeImage=image.id;
        if(g->awaiting) {
            // plan() calls this only when the timer, started at TX completion, expires.
            event("TIMEOUT",IMAGE,image.id,META_ACK,0,image.bitmap,g->window,groupWait(*g,g->needManifest));
            g->awaiting=false; g->request=0; g->timeout();
            if(state.attempts>=RETRIES_PER_BURST) {
                state.attempts=0; state.deferred=true; state.due=millis()+RETRY_PAUSE_MS+esp_random()%5000;
                g->needManifest=true; g->batchMask=g->remaining=0; g->endMeasurement(); activeImage=0; return;
            }
        }
        ImagePayload info; memcpy(&info,image.payload,sizeof(info));
        if(g->needManifest) {
            if(!send(IMAGE,image.id,g_nodeId,image.target,&info,sizeof(info),MESH_HOPS,&g->request)) state.due=millis()+1000;
            return;
        }
        if(imageNotBefore && !due(imageNotBefore)) { state.due=imageNotBefore; return; }
        if(!g->batchMask) {
            uint64_t missing=completeMask(info.bytes) & ~image.bitmap;
            if(!missing) { g->needManifest=true; state.due=millis()+30; return; }
            const bool urgent=urgentTraffic();
            uint8_t limit=g->groupSize(urgent,frameAirtimeMs(sizeof(PacketHeader)+sizeof(ChunkPayload)+AUTH_TAG_BYTES));
            const uint8_t chosen=limit;
            for(uint16_t k=0;k<imageChunks(info.bytes) && limit;k++) if(missing & (uint64_t(1)<<k)) {
                g->batchMask |= uint64_t(1)<<k; --limit;
            }
            g->remaining=g->batchMask;
            // Start before enqueueing: include contention, data retries, listen
            // gaps and all query/ACK waiting in the confirmed delivery rate.
            g->beginMeasurement(millis(),chosen,!urgent && bitmapCount(g->batchMask)==chosen);
            event("GROUP",IMAGE,image.id,META_ACK,0,g->batchMask,bitmapCount(g->batchMask),groupWait(*g,false));
        }
        if(TRANSFER_MODE==TRANSFER_ADAPTIVE && urgentTraffic() && g->remaining) {
            g->ignoreMeasurement();
            uint64_t sent=g->batchMask & ~g->remaining;
            if(sent) { g->batchMask=sent; g->remaining=0; } // Close a partly sent group at a packet boundary.
            else { state.due=millis()+IMAGE_LISTEN_MS; activeImage=0; return; }
        }
        if(g->remaining) {
            uint16_t k=0; while(!(g->remaining & (uint64_t(1)<<k))) ++k;
            ChunkPayload chunk{}; chunk.index=k; uint32_t offset=k*CHUNK_BYTES;
            chunk.count=(info.bytes-offset)<CHUNK_BYTES ? info.bytes-offset : CHUNK_BYTES;
            File f=LittleFS.open(storage->imagePath(image.id),"r");
            bool ok=f && f.seek(offset) && f.read(chunk.data,chunk.count)==chunk.count; f.close();
            if(!ok || !send(GROUP_CHUNK,image.id,g_nodeId,image.target,&chunk,3+chunk.count)) {
                state.due=millis()+1000; activeImage=0;
            }
        } else if(!send(BITMAP_QUERY,image.id,g_nodeId,image.target,nullptr,0,MESH_HOPS,&g->request)) state.due=millis()+1000;
    }
    void plan() {
        if(millis()-lastPlan<(TRANSFER_MODE==TRANSFER_BASELINE ? 200U : 20U)) return; lastPlan=millis();
        size_t n=storage->count(); if(!n) return;
        int selected=-1; uint8_t best=9; Record chosen;
        for(size_t offset=0;offset<n;offset++) {
            size_t i=(cursor+offset)%n; Record r;
            if(TRANSFER_MODE!=TRANSFER_BASELINE && !storage->needsTransmission(i,g_nodeId)) continue;
            if(!due(retries[i].due) || !storage->at(i,r)) continue;
            bool relay=r.kind==SOS && r.delivery==DELIVERY_RELAY;
            if(!relay && (r.source!=g_nodeId || r.delivery!=DELIVERY_PENDING)) continue;
            if(r.kind==IMAGE && TRANSFER_MODE!=TRANSFER_BASELINE && queuedImage(r.id)) continue;
            if(r.kind==IMAGE && GROUPED_TRANSFER && activeImage && activeImage!=r.id) continue;
            if(r.kind==CHAT || r.kind==IMAGE) {
                Record report; if(!storage->get(SOS,r.link,report)) continue;
                uint64_t target=report.source==g_nodeId ? report.target : report.source;
                if(!target || (report.source==g_nodeId && report.delivery==DELIVERY_PENDING)) continue;
                if(!r.target) { r.target=target; if(!storage->save(r)) continue; }
                if(r.kind==IMAGE && !r.complete) continue;
            }
            uint8_t prio=priority(r.kind);
            if(prio<best) { selected=i; chosen=r; best=prio; }
        }
        if(selected<0) return;
        observe(chosen);
        if(chosen.kind==IMAGE && GROUPED_TRANSFER) { cursor=(selected+1)%n; planGroup(chosen,selected); return; }
        RetryState& state=retries[selected];
        if(state.timerArmed) { event("TIMEOUT",chosen.kind,chosen.id,state.waiting,0,chosen.bitmap); state.timerArmed=false; }
        if(state.deferred) { state.deferred=false; if(chosen.kind==IMAGE) state.manifest=false; }
        bool queued=false;
        if(chosen.kind==IMAGE) {
            ImagePayload info; memcpy(&info,chosen.payload,sizeof(info));
            // After a lost final ACK or a restart, a manifest obtains the receiver's completion state.
            if(!state.manifest || chosen.bitmap==completeMask(info.bytes)) {
                state.manifest=false; state.waiting=META_ACK;
                queued=send(IMAGE,chosen.id,g_nodeId,chosen.target,&info,sizeof(info));
            } else {
                uint16_t chunk=0; while(chosen.bitmap & (uint64_t(1)<<chunk)) ++chunk;
                ChunkPayload c{}; c.index=chunk; uint32_t offset=chunk*CHUNK_BYTES;
                c.count=(info.bytes-offset)<CHUNK_BYTES ? info.bytes-offset : CHUNK_BYTES;
                File f=LittleFS.open(storage->imagePath(chosen.id),"r");
                if(f && f.seek(offset) && f.read(c.data,c.count)==c.count) {
                    state.waiting=chunk; queued=send(CHUNK,chosen.id,g_nodeId,chosen.target,&c,3+c.count);
                }
                f.close();
            }
        } else queued=send(chosen.kind,chosen.id,chosen.source,chosen.target,chosen.payload,chosen.length);
        cursor=(selected+1)%n;
        if(queued) {
            state.timerArmed=true;
            state.attempts++;
            state.due=millis()+ACK_WAIT_MS+esp_random()%2000;
            if(state.attempts>=RETRIES_PER_BURST) { state.due=millis()+RETRY_PAUSE_MS+esp_random()%5000; state.attempts=0; state.deferred=true; }
            if(chosen.delivery==DELIVERY_RELAY) state.due=millis()+RETRY_PAUSE_MS+esp_random()%10000;
        } else state.due=millis()+1000;
    }
    void transmitted(const Frame& frame,bool success) {
        PacketHeader h; memcpy(&h,frame.bytes,sizeof(h));
        if(h.kind==SOS || h.kind==RESOLVE) urgentUntil=millis()+URGENT_HOLD_MS;
        if(h.kind==SOS || h.kind==RESOLVE || h.kind==CHAT) interruptRateMeasurements();
        if(h.source!=g_nodeId) return;
        uint16_t chunk=META_ACK;
        if(h.kind==CHUNK || h.kind==GROUP_CHUNK) memcpy(&chunk,frame.bytes+sizeof(h),sizeof(chunk));
        event(success ? "TX_DONE" : "TX_FAIL",h.kind,h.objectId,chunk,frame.length,0,0,0,frameAirtimeMs(frame.length));
        if(!imageFrame(h.kind) || TRANSFER_MODE==TRANSFER_BASELINE) return;
        int i=storage->find(IMAGE,h.objectId); if(i<0) return;
        RetryState& state=retries[i];
        if(!success) { state.due=millis()+1000; return; }
        if(TRANSFER_MODE==TRANSFER_TUNED) {
            // Leave the original four-attempt cooldown intact; successful ACKs can still end it.
            if(!state.deferred) state.due=millis()+fixedAckWaitMs(state.pathHops,frame.length,sizeof(PacketHeader)+sizeof(AckPayload)+AUTH_TAG_BYTES);
            return;
        }
        GroupTransfer* g=group(h.objectId); if(!g) return;
        if(h.kind==GROUP_CHUNK) {
            g->remaining &= ~(uint64_t(1)<<chunk);
            imageNotBefore=millis()+imageListenGapMs(g->pathHops,frame.length); state.due=imageNotBefore;
        } else {
            g->request=h.packetId; g->requestEnded=millis(); g->awaiting=true; state.attempts++;
            state.due=millis()+groupWait(*g,g->needManifest);
        }
    }
public:
    explicit LoRaMeshManager(StorageManager* s):storage(s) {}
    bool begin() {
        bootId=esp_random();
        Serial.printf("[TRANSFER] mode %u (%s), protocol %u\n",unsigned(TRANSFER_MODE),transferModeName(),unsigned(MESH_VERSION));
#if EXPERIMENT_LOGGING
        Serial.println("[EXP_HEADER],mode,node,boot,ms,event,kind,id,chunk,bytes,bitmap,window,rto_ms,airtime_ms,crc32");
#endif
        for(size_t i=0;i<storage->count();i++) { Record r; if(storage->at(i,r)) observe(r,true); }
        pinMode(VEXT_CTRL_PIN,OUTPUT); digitalWrite(VEXT_CTRL_PIN,LOW); pinMode(BOARD_LED_PIN,OUTPUT); delay(100);
        SPI.begin(LORA_SCK_PIN,LORA_MISO_PIN,LORA_MOSI_PIN,LORA_NSS_PIN);
        radio=new SX1262(new Module(LORA_NSS_PIN,LORA_DIO1_PIN,LORA_RESET_PIN,LORA_BUSY_PIN));
        Serial.printf("[RADIO_CONFIG] freq_mhz=%.3f bw_khz=%.1f sf=%u cr=4/%u sync=0x%02x power_dbm=%d preamble=%u dio1=%u\n",
            double(LORA_FREQUENCY),double(LORA_BANDWIDTH),unsigned(LORA_SPREADING_FACTOR),unsigned(LORA_CODING_RATE),
            unsigned(LORA_SYNC_WORD),int(LORA_OUTPUT_POWER),unsigned(LORA_PREAMBLE_LENGTH),unsigned(LORA_DIO1_PIN));
        int status=radio->begin(LORA_FREQUENCY,LORA_BANDWIDTH,LORA_SPREADING_FACTOR,LORA_CODING_RATE,LORA_SYNC_WORD,LORA_OUTPUT_POWER,LORA_PREAMBLE_LENGTH);
        if(status!=RADIOLIB_ERR_NONE) { Serial.printf("[RADIO] Init failed: %d\n",status); return false; }
        radio->setDio1Action(setLoRaRxFlag); active=true;
        bool listening=restartReceive("init");
        Serial.printf("[RADIO] RX_START %s\n",listening ? "ready" : "retrying");
        lastBeacon=millis()-BEACON_INTERVAL_MS+5000; return listening;
    }
    void handleInterruptFlag() { irq=true; }
    void recordQueued(const Record& r) { observe(r); }
    bool clearStoredData() {
        if(!storage->canClear()) return false;
        const bool resume=active;
        if(resume) {
            radio->clearDio1Action();
            if(radio->standby()!=RADIOLIB_ERR_NONE) {
                radio->setDio1Action(setLoRaRxFlag);
                return false;
            }
        }
        active=false; irq=false; transmitting=false; receiveRestartPending=false;
        digitalWrite(BOARD_LED_PIN,LOW);
        for(auto& frame:queue) frame.length=0;
        transmittingFrame.length=0;
        for(auto& retry:retries) retry=RetryState();
        for(auto& transfer:groups) transfer=GroupTransfer();
        for(auto& logged:startLogged) logged=false;
        for(auto& id:seen) id=0;
        seenAt=0; cursor=0; activeImage=0;
        imageNotBefore=urgentUntil=receivingSince=txStarted=0;
        lastPlan=lastBeacon=millis();
        const bool cleared=storage->clearAll();
        if(resume) {
            radio->setDio1Action(setLoRaRxFlag);
            active=true; restartReceive("clear");
        }
        return cleared;
    }
    bool ready() const { return active && !receiveRestartPending; }
    uint32_t rxCount() const { return receivedFrames; }
    uint32_t txCount() const { return sentFrames; }
    uint32_t invalidCount() const { return rejectedFrames; }
    uint32_t versionRejectCount() const { return versionRejects; }
    uint32_t authRejectCount() const { return authRejects; }
    uint32_t sosStoreFailureCount() const { return sosStoreFailures; }
    uint32_t maxServiceGapMs() const { return serviceGapMax; }
    uint32_t rawRxCount() const { return rawReceiveFrames; }
    uint32_t rxReadErrorCount() const { return receiveReadErrors; }
    uint32_t rxLengthErrorCount() const { return receiveLengthErrors; }
    uint32_t rxStartErrorCount() const { return receiveStartErrors; }
    uint32_t polledEventCount() const { return polledEvents; }
    int lastRxError() const { return lastReceiveError; }
    float lastRssi() const { return rssi; }
    void retryNow() { for(auto& s:retries) { s.due=millis()+esp_random()%1000; s.attempts=0; s.deferred=false; } }
    void beacon() { if(CURRENT_NODE_ROLE==ROLE_RESCUER) { send(BEACON,newId(),g_nodeId,0,nullptr,0); lastBeacon=millis(); } }
    void update() {
        const uint32_t now=millis();
        if(lastService && now-lastService>serviceGapMax) serviceGapMax=now-lastService;
        lastService=now;
        if(!active) return;
        if(receiveRestartPending) {
            if(due(nextReceiveRetry)) restartReceive("retry");
            return;
        }
        if(urgentUntil && due(urgentUntil)) urgentUntil=0;
        if(imageNotBefore && due(imageNotBefore)) imageNotBefore=0;
        // Poll completed events even when there are no queued transmissions.
        // This covers a missed DIO1 edge without restarting a packet in flight.
        if(irq || now-lastIrqPoll>=10) {
            const bool signalled=irq;
            irq=false;
            lastIrqPoll=now;
            const uint32_t flags=radio->getIrqFlags();
            if(transmitting && (flags & RADIOLIB_SX126X_IRQ_TX_DONE)) {
                if(!signalled) ++polledEvents;
                radio->finishTransmit(); transmitting=false; sentFrames++; digitalWrite(BOARD_LED_PIN,LOW);
                transmitted(transmittingFrame,true);
                restartReceive("tx_done");
            }
            else if(!transmitting && (flags & RADIOLIB_SX126X_IRQ_RX_DONE)) {
                if(!signalled) ++polledEvents;
                ++rawReceiveFrames;
                size_t n=radio->getPacketLength(); uint8_t frame[255];
                if(n>0 && n<=sizeof(frame)) {
                    const int result=radio->readData(frame,n);
                    if(result!=RADIOLIB_ERR_NONE) receiveError(result,n,flags);
                    else if(n>=sizeof(PacketHeader)+AUTH_TAG_BYTES) { rssi=radio->getRSSI(); incoming(frame,n); }
                    else ++receiveLengthErrors;
                } else ++receiveLengthErrors;
                restartReceive("rx_done");
            } else if(!transmitting && ((flags & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_TIMEOUT)) ||
                ((flags & RADIOLIB_SX126X_IRQ_HEADER_ERR) && !(flags & RADIOLIB_SX126X_IRQ_HEADER_VALID)))) {
                receiveError((flags & RADIOLIB_SX126X_IRQ_TIMEOUT) ? RADIOLIB_ERR_RX_TIMEOUT : RADIOLIB_ERR_CRC_MISMATCH,0,flags);
                restartReceive("rx_error");
            }
            // Ignore unrelated/stale CAD interrupts. They must not abort RX or
            // mark a still-running transmission complete.
        }
        if(receiveRestartPending) return;
        if(transmitting) {
            if(millis()-txStarted>10000) {
                radio->finishTransmit(); transmitting=false; restartReceive("tx_timeout"); digitalWrite(BOARD_LED_PIN,LOW);
                transmitted(transmittingFrame,false);
            }
            return;
        }
        if(CURRENT_NODE_ROLE==ROLE_RESCUER && millis()-lastBeacon>=BEACON_INTERVAL_MS) beacon();
        plan();
        int next=-1;
        for(size_t i=0;i<16;i++) if(queue[i].length && due(queue[i].due)) {
            PacketHeader h; memcpy(&h,queue[i].bytes,sizeof(h));
            if(GROUPED_TRANSFER && h.source==g_nodeId && imageFrame(h.kind) && imageNotBefore && !due(imageNotBefore)) continue;
            if(next<0 || queue[i].priority<queue[next].priority) next=i;
        }
        if(next<0) return;
        if(TRANSFER_MODE!=TRANSFER_BASELINE) {
            const uint32_t flags=radio->getIrqFlags();
            if(flags & RADIOLIB_SX126X_IRQ_RX_DONE) { irq=true; return; }
            // CAD switches the radio out of receive mode. Let a detected packet
            // finish first, including an SOS that began inside a listening gap.
            if((flags & RADIOLIB_SX126X_IRQ_HEADER_VALID) &&
               !(flags & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_TIMEOUT))) {
                if(!receivingSince) receivingSince=millis();
                if(millis()-receivingSince < frameAirtimeMs(255)+300) return;
                receiveError(RADIOLIB_ERR_RX_TIMEOUT,0,flags);
                restartReceive("header_timeout"); return;
            }
            receivingSince=0;
        }
        int cad=radio->scanChannel();
        if(cad!=RADIOLIB_CHANNEL_FREE) { queue[next].due=millis()+300+esp_random()%1000; restartReceive("cad_busy"); return; }
        irq=false; transmitting=true; txStarted=millis(); digitalWrite(BOARD_LED_PIN,HIGH);
        transmittingFrame=queue[next];
        int result=radio->startTransmit(queue[next].bytes,queue[next].length);
        queue[next].length=0;
        if(result!=RADIOLIB_ERR_NONE) {
            transmitting=false; digitalWrite(BOARD_LED_PIN,LOW); restartReceive("tx_start_failed"); transmitted(transmittingFrame,false);
        } else {
            PacketHeader h; memcpy(&h,transmittingFrame.bytes,sizeof(h));
            if(h.source==g_nodeId) {
                uint16_t chunk=META_ACK;
                if(h.kind==CHUNK || h.kind==GROUP_CHUNK) memcpy(&chunk,transmittingFrame.bytes+sizeof(h),sizeof(chunk));
                event("TX_START",h.kind,h.objectId,chunk,transmittingFrame.length,0,0,0,frameAirtimeMs(transmittingFrame.length));
            }
        }
    }
};
