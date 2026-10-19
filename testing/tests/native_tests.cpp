#include <cassert>
#include <iostream>
#define HELTEC_TEST
#include "LoRaMeshManager.h"
#include "PortalRouting.h"
#include "BufferedOutput.h"
uint64_t g_nodeId=0x123456;
void setLoRaRxFlag(){}
struct MeshTestProbe {
    static std::vector<uint8_t> frame(LoRaMeshManager& mesh,uint8_t kind,uint64_t id,uint64_t from,uint64_t to,const void* p,size_t n){
        PacketHeader h{MESH_MAGIC,MESH_VERSION,kind,newId(),from,to,id,3,(uint16_t)n};
        std::vector<uint8_t> f(sizeof(h)+n+AUTH_TAG_BYTES);memcpy(f.data(),&h,sizeof(h));if(n)memcpy(f.data()+sizeof(h),p,n);mesh.tag(f.data(),sizeof(h)+n,false);return f;
    }
    static void receive(LoRaMeshManager& mesh,std::vector<uint8_t> f){mesh.incoming(f.data(),f.size());}
    static int queued(LoRaMeshManager& m,uint8_t kind){int n=0;for(auto& q:m.queue)if(q.length && q.bytes[3]==kind)n++;return n;}
    static void clear(LoRaMeshManager& m){for(auto& q:m.queue)q.length=0;}
    static void plan(LoRaMeshManager& m){fakeMillis+=201;m.plan();}
    static RetryState& retry(LoRaMeshManager& m,int i){return m.retries[i];}
    static GroupTransfer& group(LoRaMeshManager& m,uint64_t id){return *m.group(id);}
    static PacketHeader header(const std::vector<uint8_t>& f){PacketHeader h;memcpy(&h,f.data(),sizeof(h));return h;}
    static std::vector<uint8_t> take(LoRaMeshManager& m,uint8_t kind,bool finish=true){
        for(auto& q:m.queue)if(q.length && q.bytes[3]==kind){
            auto copy=q;std::vector<uint8_t> f(q.bytes,q.bytes+q.length);q.length=0;
            if(finish){fakeMillis+=frameAirtimeMs(copy.length);m.transmitted(copy,true);}return f;
        }assert(false && "expected queued packet");return {};
    }
    static void duePlan(LoRaMeshManager& m,int i){fakeMillis=m.retries[i].due+201;m.plan();}
    static uint8_t transmittingKind(LoRaMeshManager& m){return m.transmitting ? m.transmittingFrame.bytes[3] : 0;}
    static void receivingHeader(LoRaMeshManager& m,bool yes){m.radio->flags=yes ? RADIOLIB_SX126X_IRQ_HEADER_VALID : 0;}
    static SX1262& radio(LoRaMeshManager& m){return *m.radio;}
    static void seedClearState(LoRaMeshManager& m){
        for(auto& q:m.queue){q.length=10;q.priority=0;}
        m.transmitting=true;m.transmittingFrame.length=10;m.irq=true;
        for(auto& r:m.retries){r.due=9999;r.attempts=3;r.manifest=r.deferred=r.timerArmed=true;}
        for(auto& g:m.groups){g.id=55;g.awaiting=true;g.request=99;}
        for(auto& b:m.startLogged)b=true;
        for(auto& id:m.seen)id=77;
        m.activeImage=55;m.imageNotBefore=m.urgentUntil=m.receivingSince=9999;m.cursor=20;
    }
    static void assertClearState(LoRaMeshManager& m){
        for(auto& q:m.queue)assert(!q.length);
        assert(!m.transmitting && !m.transmittingFrame.length && !m.irq);
        for(auto& r:m.retries)assert(!r.due && !r.attempts && !r.manifest && !r.deferred && !r.timerArmed);
        for(auto& g:m.groups)assert(!g.id && !g.awaiting && !g.request && g.needManifest);
        for(auto b:m.startLogged)assert(!b);
        for(auto id:m.seen)assert(!id);
        assert(!m.activeImage && !m.imageNotBefore && !m.urgentUntil && !m.receivingSince && !m.cursor);
    }
};
Record sos(uint64_t id,uint64_t from=g_nodeId){Record r;r.kind=SOS;r.id=id;r.source=from;r.length=sizeof(SosPayload);SosPayload p{};p.people=2;p.gps=1;p.latitude=0;p.longitude=0;strcpy(p.text,"Help");memcpy(r.payload,&p,sizeof(p));return r;}
void portalTests(){
    assert(isPortalHost("192.168.4.1"));assert(isPortalHost("192.168.4.1:443"));
    assert(!isPortalHost("192.168.4.1.evil.test"));assert(!isPortalHost("192.168.4.1:80"));
    assert(!redirectToPortal("GET","192.168.4.1","/"));
    assert(!redirectToPortal("GET","192.168.4.1","/app.js"));
    assert(!redirectToPortal("GET","192.168.4.1","/api/state"));
    assert(redirectToPortal("GET","connectivitycheck.gstatic.com","/generate_204"));
    assert(redirectToPortal("GET","captive.apple.com","/hotspot-detect.html"));
    assert(redirectToPortal("GET","www.msftconnecttest.com","/connecttest.txt"));
    assert(redirectToPortal("GET","192.168.4.1","/generate_204"));
    assert(redirectToPortal("GET","heltec-lora.local","/"));
    assert(redirectToPortal("HEAD","192.168.4.1","/unknown"));
    assert(!redirectToPortal("POST","untrusted.test","/api/sos"));
    assert(strcmp(PORTAL_URL,"https://192.168.4.1/")==0);
}
void protocolTests(){
    StorageManager s;LoRaMeshManager m(&s);Record r=sos(99);auto f=MeshTestProbe::frame(m,SOS,r.id,r.source,0,r.payload,r.length);PacketHeader h;
    assert(parseFrame(f.data(),f.size(),h));assert(!parseFrame(f.data(),f.size()-1,h));
    f[2]=2;assert(!parseFrame(f.data(),f.size(),h));f[2]=MESH_VERSION;f[36]=0;assert(!parseFrame(f.data(),f.size(),h));
    SosPayload p{};p.people=1;p.gps=1;p.latitude=91;auto bad=MeshTestProbe::frame(m,SOS,1,2,0,&p,sizeof(p));assert(!parseFrame(bad.data(),bad.size(),h));
    ChunkPayload c{};c.index=46;c.count=1;bad=MeshTestProbe::frame(m,CHUNK,1,2,3,&c,4);assert(!parseFrame(bad.data(),bad.size(),h));
    assert(crc32("123456789",9)==0xcbf43926u);assert(imageChunks(8192)==46);assert(sizeof(PacketHeader)+sizeof(SosPayload)+8<=255);
}
void persistenceTests(){
    LittleFS.reset();StorageManager s;assert(s.begin());Record r=sos(77);assert(s.save(r));Record old;assert(s.get(SOS,77,old));
    r.delivery=DELIVERY_RECEIVED;LittleFS.failRename=true;assert(!s.save(r));LittleFS.failRename=false;
    assert(s.needsTransmission(s.find(SOS,77),g_nodeId)); // Failed commits must not suppress retries.
    StorageManager reboot;assert(reboot.begin());assert(reboot.get(SOS,77,r));assert(r.delivery==DELIVERY_PENDING);assert(reboot.count()==1);
    assert(reboot.save(r));r.delivery=DELIVERY_RECEIVED;assert(reboot.save(r));assert(!reboot.resolved(r.id));assert(!reboot.needsTransmission(reboot.find(SOS,77),g_nodeId));
    Record resolved;resolved.kind=RESOLVE;resolved.id=78;resolved.source=9;resolved.link=r.id;ResolvePayload p{r.id,0};resolved.length=sizeof(p);memcpy(resolved.payload,&p,sizeof(p));assert(reboot.save(resolved));
    StorageManager again;assert(again.begin());assert(again.resolved(77));
    LittleFS.failWrites=true;Record fresh=sos(100);assert(!again.save(fresh));assert(!again.get(SOS,100,r));LittleFS.failWrites=false;
    LittleFS.capacity=LittleFS.usedBytes()+1000;assert(!again.save(fresh));
    LittleFS.reset();StorageManager full;assert(full.begin());for(size_t i=0;i<MAX_RECORDS;i++){auto x=sos(i+1);assert(full.save(x));}fresh=sos(1000);assert(!full.save(fresh));assert(full.count()==MAX_RECORDS);
    LittleFS.reset();LittleFS.mount=false;StorageManager unmounted;assert(!unmounted.begin());assert(!unmounted.save(fresh));
}
std::vector<uint8_t> jpeg(size_t size=400){std::vector<uint8_t> b(size);for(size_t i=0;i<b.size();i++)b[i]=uint8_t(i);b[0]=0xff;b[1]=0xd8;b[size-2]=0xff;b[size-1]=0xd9;return b;}
ChunkPayload chunk(const std::vector<uint8_t>& b,int index){ChunkPayload c{};c.index=index;size_t start=index*CHUNK_BYTES;c.count=(uint8_t)std::min(CHUNK_BYTES,b.size()-start);memcpy(c.data,b.data()+start,c.count);return c;}
void imageTests(){
    LittleFS.reset();StorageManager s;assert(s.begin());auto bytes=jpeg();ImagePayload p{55,(uint32_t)bytes.size(),crc32(bytes.data(),bytes.size())};
    PacketHeader h{};h.kind=IMAGE;h.objectId=55;h.source=9;h.length=sizeof(p);Record r;assert(s.beginIncomingImage(h,p,r));
    assert(s.receiveChunk(r,chunk(bytes,1))==1);assert(s.receiveChunk(r,chunk(bytes,0))==1);assert(s.receiveChunk(r,chunk(bytes,0))==1);
    StorageManager reboot;assert(reboot.begin());assert(reboot.get(IMAGE,55,r));assert(r.bitmap==3 && !r.complete);
    assert(reboot.receiveChunk(r,chunk(bytes,2))==2);assert(r.complete);assert(reboot.verifyImage(55,p,true));
    assert(reboot.receiveChunk(r,chunk(bytes,0))==2); // Duplicate final-ACK recovery cannot truncate the image.
    assert(reboot.beginIncomingImage(h,p,r) && r.complete);
    LittleFS.reset();StorageManager bad;assert(bad.begin());assert(bad.beginIncomingImage(h,p,r));
    auto corrupt=chunk(bytes,1);corrupt.data[5]^=1;assert(bad.receiveChunk(r,corrupt)==1);assert(bad.receiveChunk(r,chunk(bytes,0))==1);assert(bad.receiveChunk(r,chunk(bytes,2))==3);assert(r.bitmap==0 && !r.complete);
    LittleFS.failWrites=true;assert(bad.receiveChunk(r,chunk(bytes,0))==0);LittleFS.failWrites=false;
    // Simulate power loss after final JPEG rename but before complete=true was committed.
    LittleFS.reset();StorageManager interrupted;assert(interrupted.begin());assert(interrupted.beginIncomingImage(h,p,r));
    File jpg=LittleFS.open(interrupted.imagePath(55),"w");assert(jpg.write(bytes.data(),bytes.size())==bytes.size());jpg.close();
    StorageManager afterRename;assert(afterRename.begin());assert(afterRename.get(IMAGE,55,r));assert(!r.complete);
    assert(afterRename.receiveChunk(r,chunk(bytes,2))==2);assert(r.complete);
    // A bitmap query can repair finalisation without requiring another data chunk.
    LittleFS.reset();StorageManager queried;assert(queried.begin());assert(queried.beginIncomingImage(h,p,r));
    File part=LittleFS.open(queried.imagePath(55,false),"w");assert(part.write(bytes.data(),bytes.size())==bytes.size());part.close();
    r.bitmap=completeMask(p.bytes);assert(queried.save(r));
    LittleFS.failRename=true;assert(queried.imageReceipt(r)==0);LittleFS.failRename=false;
    assert(queried.get(IMAGE,55,r));assert(queried.imageReceipt(r)==2);assert(r.complete);
}
void legacyTests(){
#if CURRENT_NODE_ROLE == ROLE_CIVILIAN
    LittleFS.reset();uint8_t old[124]={};old[0]=99;old[4]=2;old[5]=3;memcpy(old+38,"Recovered help",15);
    File f=LittleFS.open("/unsent_sos.bin","w");assert(f.write(old,sizeof(old))==124);f.close();
    StorageManager s;assert(s.begin());assert(s.count()==1 && s.unclaimed()==1);Record r;assert(s.at(0,r));assert(r.delivery==DELIVERY_PENDING);
    assert(s.recoverFor("0123456789abcdef0123456789abcdef"));assert(s.unclaimed()==0);
    StorageManager reboot;assert(reboot.begin());assert(reboot.count()==1);assert(LittleFS.exists("/unsent_sos.bin"));
#endif
}
void meshTests(){
    LittleFS.reset();StorageManager s;assert(s.begin());LoRaMeshManager mesh(&s);assert(mesh.begin());
#if CURRENT_NODE_ROLE == ROLE_CIVILIAN
    Record r=sos(500);assert(s.save(r));MeshTestProbe::plan(mesh);assert(MeshTestProbe::queued(mesh,SOS)==1);
    MeshTestProbe::clear(mesh);fakeMillis+=ACK_WAIT_MS+3000;MeshTestProbe::plan(mesh);assert(MeshTestProbe::queued(mesh,SOS)==1);assert(s.get(SOS,500,r)&&r.delivery==DELIVERY_PENDING);
    AckPayload a{SOS,META_ACK,0};auto f=MeshTestProbe::frame(mesh,ACK,500,9,g_nodeId,&a,sizeof(a));auto corrupt=f;corrupt.back()^=1;MeshTestProbe::receive(mesh,corrupt);assert(s.get(SOS,500,r)&&r.delivery==DELIVERY_PENDING);
    MeshTestProbe::receive(mesh,f);assert(s.get(SOS,500,r)&&r.delivery==DELIVERY_RECEIVED && r.target==9);assert(!s.resolved(500));
    ChatPayload chat{500,0,"We are coming"};auto message=MeshTestProbe::frame(mesh,CHAT,600,9,g_nodeId,&chat,sizeof(chat));MeshTestProbe::receive(mesh,message);assert(s.get(CHAT,600,r));size_t count=s.count();
    message=MeshTestProbe::frame(mesh,CHAT,600,9,g_nodeId,&chat,sizeof(chat));MeshTestProbe::receive(mesh,message);assert(s.count()==count); // Fresh packet retry, same logical message.
    chat.report=999;message=MeshTestProbe::frame(mesh,CHAT,601,9,g_nodeId,&chat,sizeof(chat));MeshTestProbe::receive(mesh,message);assert(!s.get(CHAT,601,r));
#if TRANSFER_MODE <= TRANSFER_TUNED
    Record img;img.kind=IMAGE;img.id=500;img.link=500;img.source=g_nodeId;img.target=9;img.complete=1;
    auto photo=jpeg();ImagePayload info{500,(uint32_t)photo.size(),crc32(photo.data(),photo.size())};img.length=sizeof(info);memcpy(img.payload,&info,sizeof(info));assert(s.save(img));
    File file=LittleFS.open(s.imagePath(img.id),"w");assert(file.write(photo.data(),photo.size())==photo.size());file.close();
    MeshTestProbe::clear(mesh);MeshTestProbe::plan(mesh);assert(MeshTestProbe::queued(mesh,IMAGE)==1);MeshTestProbe::take(mesh,IMAGE);
    AckPayload meta{IMAGE,META_ACK,0};MeshTestProbe::receive(mesh,MeshTestProbe::frame(mesh,ACK,500,9,g_nodeId,&meta,sizeof(meta)));
    int ii=s.find(IMAGE,500);MeshTestProbe::retry(mesh,ii).attempts=RETRIES_PER_BURST-1;fakeMillis+=400;MeshTestProbe::plan(mesh);
    assert(MeshTestProbe::queued(mesh,CHUNK)==1); // ACK arriving after the final burst attempt must still count.
    AckPayload chunkAck{IMAGE,0,0};MeshTestProbe::receive(mesh,MeshTestProbe::frame(mesh,ACK,500,9,g_nodeId,&chunkAck,sizeof(chunkAck)));
    assert(s.get(IMAGE,500,img)&&img.bitmap==1&&img.delivery==DELIVERY_PENDING);
    // Delayed manifest ACK must not reset an in-flight chunk.
    MeshTestProbe::retry(mesh,ii).waiting=1;auto oldDue=MeshTestProbe::retry(mesh,ii).due;
    MeshTestProbe::receive(mesh,MeshTestProbe::frame(mesh,ACK,500,9,g_nodeId,&meta,sizeof(meta)));assert(MeshTestProbe::retry(mesh,ii).due==oldDue);
    // Lost CRC-restart ACK: a later incomplete manifest must restart an apparently full bitmap.
    assert(s.get(IMAGE,500,img));img.bitmap=7;assert(s.save(img));MeshTestProbe::retry(mesh,ii).manifest=false;
    MeshTestProbe::receive(mesh,MeshTestProbe::frame(mesh,ACK,500,9,g_nodeId,&meta,sizeof(meta)));
    assert(s.get(IMAGE,500,img)&&img.bitmap==0&&img.delivery==DELIVERY_PENDING);
    AckPayload complete{IMAGE,META_ACK,ACK_COMPLETE};MeshTestProbe::receive(mesh,MeshTestProbe::frame(mesh,ACK,500,9,g_nodeId,&complete,sizeof(complete)));
    assert(s.get(IMAGE,500,img)&&img.delivery==DELIVERY_RECEIVED&&img.bitmap==7);
#endif
    ResolvePayload resolution{500,0};message=MeshTestProbe::frame(mesh,RESOLVE,700,9,g_nodeId,&resolution,sizeof(resolution));MeshTestProbe::receive(mesh,message);assert(s.resolved(500));
#else
    Record incoming=sos(500,9);auto f=MeshTestProbe::frame(mesh,SOS,500,9,0,incoming.payload,incoming.length);
    LittleFS.failWrites=true;MeshTestProbe::receive(mesh,f);assert(MeshTestProbe::queued(mesh,ACK)==0);assert(s.count()==0);LittleFS.failWrites=false;
    f=MeshTestProbe::frame(mesh,SOS,500,9,0,incoming.payload,incoming.length);MeshTestProbe::receive(mesh,f);assert(MeshTestProbe::queued(mesh,ACK)==1);assert(s.count()==1);
    MeshTestProbe::clear(mesh);f=MeshTestProbe::frame(mesh,SOS,500,9,0,incoming.payload,incoming.length);MeshTestProbe::receive(mesh,f);assert(MeshTestProbe::queued(mesh,ACK)==1 && s.count()==1);
    ChatPayload chat{500,0,"Still here"};f=MeshTestProbe::frame(mesh,CHAT,600,9,g_nodeId,&chat,sizeof(chat));MeshTestProbe::receive(mesh,f);Record r;assert(s.get(CHAT,600,r));
#endif
}
void policyTests(){
    assert(frameAirtimeMs(230)==1574);assert(frameAirtimeMs(51)==428);
    assert(ackPathHops(3)==1 && ackPathHops(1)==3);
    assert(imageListenGapMs(3,230)>imageListenGapMs(1,230));
    assert(fixedAckWaitMs(3,230,51)>fixedAckWaitMs(1,230,51));
    assert(bitmapDataBytes(7,400)==400 && bitmapDataBytes(4,400)==40);
    assert(bitmapDataBytes(completeMask(8192),8192)==8192);
    GroupTransfer g;g.feedback(800,3000);g.feedback(800,3000);
    assert(g.rto>=3000);
    if(TRANSFER_MODE==TRANSFER_FIXED)assert(g.groupSize(true,1574)==FIXED_GROUP_CHUNKS);
#if TRANSFER_MODE == TRANSFER_ADAPTIVE
    // Timing replies alone cannot increase/decrease the image window.
    assert(g.window==ADAPTIVE_INITIAL_CHUNKS && !g.ratePolicy.measured);
    assert(GroupTransfer::groupCap(1574)==5 && GroupTransfer::groupCap(10000)==1);
    assert(GroupTransfer::groupCap(1)==ADAPTIVE_MAX_CHUNKS);
    // A lost ACK preserves the window; the eventual rate includes its wait.
    g.beginMeasurement(1000,2,true);g.timeout();
    assert(g.window==2 && g.unanswered==1 && g.rto>=6000);
    assert(g.finishMeasurement(7000,360,false,5));
    assert(g.ratePolicy.preferredRate==60 && g.ratePolicy.preferred==2 && g.window==4);
    // A larger trial is retained only when confirmed delivery gets faster.
    g.beginMeasurement(8000,4,true);assert(g.finishMeasurement(17000,720,false,5));
    assert(g.ratePolicy.preferred==4 && g.ratePolicy.preferredRate==80 && g.window==5);
    g.beginMeasurement(18000,5,true);assert(g.finishMeasurement(33000,900,false,5));
    assert(g.ratePolicy.preferred==4 && g.window==4); // 60 B/s loses to 80 B/s.
    g.srtt=10000;assert(g.groupSize(false,1574)==4 && g.groupSize(true,1574)==1);
    // One more lost query still does not shrink; repeated unanswered requests do.
    g.unanswered=0;g.beginMeasurement(34000,4,true);g.timeout();assert(g.window==4);
    g.timeout();assert(g.window==2 && !g.sampleEligible);
    assert(!g.finishMeasurement(55000,720,false,5) && !g.ratePolicy.measured);
    for(int k=0;k<10;k++)g.timeout();assert(g.rto==TRANSFER_RTO_MAX_MS);

    AdaptiveRatePolicy policy;
    policy.observe(2,100,false,5);assert(policy.selected==4);
    policy.observe(4,105,false,5);assert(policy.preferred==2 && policy.selected==2); // 5% margin.
    for(int k=0;k<ADAPTIVE_PROBE_INTERVAL_GROUPS;k++)policy.observe(2,100,false,5);
    assert(policy.selected==1);policy.observe(1,130,false,5);assert(policy.preferred==1);
    policy.applyGroupCap(1);assert(policy.selected==1 && policy.preferred==1);
    for(int k=0;k<10;k++)policy.observe(1,100,false,1);assert(policy.selected==1);
    // A bitmap with actual missing data tries a smaller size and compares rates.
    AdaptiveRatePolicy missing;missing.observe(2,50,true,5);
    assert(missing.preferred==2 && missing.selected==1);
    missing.observe(1,80,false,5);assert(missing.preferred==1);
    AdaptiveRatePolicy zero;zero.observe(2,0,true,5);
    assert(zero.measured && zero.selected==1);zero.observe(1,10,false,5);assert(zero.preferred==1);

    // Tail/urgent groups and ACK-only exchanges must not train the size policy.
    GroupTransfer excluded;excluded.beginMeasurement(100,2,false);
    assert(!excluded.finishMeasurement(1100,180,false,5) && !excluded.ratePolicy.measured);
    excluded.beginMeasurement(2000,2,true);excluded.ignoreMeasurement();
    assert(!excluded.finishMeasurement(3000,360,false,5) && !excluded.ratePolicy.measured);
    assert(!excluded.finishMeasurement(4000,360,false,5));
    excluded.beginMeasurement(5000,2,true);assert(!excluded.finishMeasurement(5000,360,false,5));
    // Unsigned clock subtraction remains correct across millis() wraparound.
    GroupTransfer wrapped;wrapped.beginMeasurement(0xfffffff0u,2,true);
    assert(wrapped.finishMeasurement(984,360,false,5));assert(wrapped.ratePolicy.preferredRate==360);
#endif
}
void groupedTests(){
#if TRANSFER_MODE >= TRANSFER_FIXED
    LittleFS.reset();StorageManager s;assert(s.begin());LoRaMeshManager m(&s);assert(m.begin());
    auto bytes=jpeg(180*10+37);ImagePayload info{800,(uint32_t)bytes.size(),crc32(bytes.data(),bytes.size())};
    Record report=sos(800,CURRENT_NODE_ROLE==ROLE_CIVILIAN ? g_nodeId : 9);report.target=CURRENT_NODE_ROLE==ROLE_CIVILIAN ? 9 : g_nodeId;
    report.delivery=DELIVERY_RECEIVED;assert(s.save(report));
#if CURRENT_NODE_ROLE == ROLE_CIVILIAN
    Record img;img.kind=IMAGE;img.id=img.link=800;img.source=g_nodeId;img.target=9;img.complete=1;img.length=sizeof(info);memcpy(img.payload,&info,sizeof(info));assert(s.save(img));
    File f=LittleFS.open(s.imagePath(800),"w");assert(f.write(bytes.data(),bytes.size())==bytes.size());f.close();
    int i=s.find(IMAGE,800);MeshTestProbe::plan(m);assert(MeshTestProbe::queued(m,IMAGE)==1);
    // A frame held in the queue must not time out or be queued twice.
    fakeMillis+=ACK_WAIT_MS*2;MeshTestProbe::plan(m);assert(MeshTestProbe::queued(m,IMAGE)==1);
    auto manifest=MeshTestProbe::take(m,IMAGE);auto request=MeshTestProbe::header(manifest);
    assert(MeshTestProbe::retry(m,i).due>fakeMillis);
    auto answer=[&](uint64_t req,uint64_t mask,uint8_t flags=0){BitmapAckPayload a{req,mask,flags};MeshTestProbe::receive(m,MeshTestProbe::frame(m,BITMAP_ACK,800,9,g_nodeId,&a,sizeof(a)));};
    fakeMillis+=800;answer(request.packetId,0);assert(!MeshTestProbe::group(m,800).needManifest);
    MeshTestProbe::duePlan(m,i);auto& g=MeshTestProbe::group(m,800);uint64_t first=g.batchMask;
    assert(bitmapCount(first)==(TRANSFER_MODE==TRANSFER_FIXED ? 4 : 2));
    // Queue eviction before transmission must not mark the chunk as sent.
    auto displaced=MeshTestProbe::take(m,GROUP_CHUNK,false);uint16_t displacedIndex;memcpy(&displacedIndex,displaced.data()+sizeof(PacketHeader),2);
    assert(g.remaining & (uint64_t(1)<<displacedIndex));MeshTestProbe::plan(m);assert(MeshTestProbe::queued(m,GROUP_CHUNK)==1);
    uint64_t sent=0;
    while(MeshTestProbe::queued(m,GROUP_CHUNK)){
        auto frame=MeshTestProbe::take(m,GROUP_CHUNK);ChunkPayload c{};memcpy(&c,frame.data()+sizeof(PacketHeader),MeshTestProbe::header(frame).length);sent|=uint64_t(1)<<c.index;
        assert(!MeshTestProbe::queued(m,BITMAP_QUERY));MeshTestProbe::duePlan(m,i);
    }
    assert(sent==first && MeshTestProbe::queued(m,BITMAP_QUERY)==1);
    auto query=MeshTestProbe::take(m,BITMAP_QUERY);auto qh=MeshTestProbe::header(query);
    // A delayed manifest or an out-of-range/false completion response cannot advance progress.
    answer(request.packetId,completeMask(info.bytes),ACK_COMPLETE);assert(s.get(IMAGE,800,img)&&img.bitmap==0);
    answer(qh.packetId,uint64_t(1)<<45);assert(s.get(IMAGE,800,img)&&img.bitmap==0);
    answer(qh.packetId,1,ACK_COMPLETE);assert(s.get(IMAGE,800,img)&&img.delivery==DELIVERY_PENDING);
    LittleFS.failRename=true;answer(qh.packetId,first);assert(s.get(IMAGE,800,img)&&img.bitmap==0);LittleFS.failRename=false;
    // Lose the bitmap ACK. Only the query is retried, with a fresh correlation ID.
    const uint8_t beforeTimeout=g.window;
    MeshTestProbe::duePlan(m,i);assert(!MeshTestProbe::queued(m,GROUP_CHUNK));assert(MeshTestProbe::queued(m,BITMAP_QUERY)==1);
    assert(g.window==beforeTimeout); // Missing feedback alone does not prove data loss.
    auto retry=MeshTestProbe::take(m,BITMAP_QUERY);auto rh=MeshTestProbe::header(retry);assert(rh.packetId!=qh.packetId);
    answer(qh.packetId,first);assert(s.get(IMAGE,800,img)&&img.bitmap==0);
    uint64_t partial=first & ~uint64_t(2);fakeMillis+=700;answer(rh.packetId,partial);assert(s.get(IMAGE,800,img)&&img.bitmap==partial);
    MeshTestProbe::duePlan(m,i);auto next=MeshTestProbe::take(m,GROUP_CHUNK);ChunkPayload c{};memcpy(&c,next.data()+sizeof(PacketHeader),MeshTestProbe::header(next).length);
    assert(c.index==1);assert(!(MeshTestProbe::group(m,800).batchMask & partial));
    // Fresh sender state obtains durable receiver progress instead of starting at zero.
    StorageManager reboot;assert(reboot.begin());LoRaMeshManager restored(&reboot);assert(restored.begin());
    i=reboot.find(IMAGE,800);MeshTestProbe::plan(restored);auto resumed=MeshTestProbe::take(restored,IMAGE);auto resumedHeader=MeshTestProbe::header(resumed);
    BitmapAckPayload snapshot{resumedHeader.packetId,7,0};fakeMillis+=500;
    MeshTestProbe::receive(restored,MeshTestProbe::frame(restored,BITMAP_ACK,800,9,g_nodeId,&snapshot,sizeof(snapshot)));
    assert(reboot.get(IMAGE,800,img)&&img.bitmap==7);MeshTestProbe::duePlan(restored,i);
    auto missing=MeshTestProbe::take(restored,GROUP_CHUNK);memcpy(&c,missing.data()+sizeof(PacketHeader),MeshTestProbe::header(missing).length);assert(c.index==3);
    // SOS frames win over queued image data. The current radio packet cannot be preempted.
    MeshTestProbe::duePlan(restored,i);Record urgent=sos(900,22);
    MeshTestProbe::receive(restored,MeshTestProbe::frame(restored,SOS,900,22,0,urgent.payload,urgent.length));fakeMillis+=2500;
    MeshTestProbe::receivingHeader(restored,true);restored.update();assert(!MeshTestProbe::transmittingKind(restored));
    MeshTestProbe::receivingHeader(restored,false);restored.update();assert(MeshTestProbe::transmittingKind(restored)==SOS);
    // Even an ACK after the fourth request is accepted; completion is persisted.
    // Use a fresh manager as after a reboot, without the earlier in-flight SOS.
    LoRaMeshManager finalSender(&reboot);assert(finalSender.begin());
    MeshTestProbe::plan(finalSender);
    // The received relay SOS is also eligible; exclude it from this completion test.
    MeshTestProbe::clear(finalSender);Record held;assert(reboot.get(SOS,900,held));held.delivery=DELIVERY_RECEIVED;assert(reboot.save(held));
    MeshTestProbe::plan(finalSender);
    auto finalRequest=MeshTestProbe::take(finalSender,IMAGE);
    for(int attempt=1;attempt<4;attempt++) { MeshTestProbe::duePlan(finalSender,i);finalRequest=MeshTestProbe::take(finalSender,IMAGE); }
    BitmapAckPayload done{MeshTestProbe::header(finalRequest).packetId,completeMask(info.bytes),ACK_COMPLETE};
    MeshTestProbe::receive(finalSender,MeshTestProbe::frame(finalSender,BITMAP_ACK,800,9,g_nodeId,&done,sizeof(done)));
    assert(reboot.get(IMAGE,800,img)&&img.delivery==DELIVERY_RECEIVED&&img.bitmap==completeMask(info.bytes));
#else
    auto manifest=MeshTestProbe::frame(m,IMAGE,800,9,g_nodeId,&info,sizeof(info));
    MeshTestProbe::receive(m,manifest);assert(MeshTestProbe::queued(m,BITMAP_ACK)==1);
    auto reply=MeshTestProbe::take(m,BITMAP_ACK,false);BitmapAckPayload a;memcpy(&a,reply.data()+sizeof(PacketHeader),sizeof(a));
    assert(a.request==MeshTestProbe::header(manifest).packetId && !a.bitmap && !a.flags);
    auto receiveChunk=[&](int k,bool corrupt=false){auto c=chunk(bytes,k);if(corrupt)c.data[5]^=1;MeshTestProbe::receive(m,MeshTestProbe::frame(m,GROUP_CHUNK,800,9,g_nodeId,&c,3+c.count));};
    receiveChunk(1);receiveChunk(0);receiveChunk(1);assert(!MeshTestProbe::queued(m,ACK)&&!MeshTestProbe::queued(m,BITMAP_ACK));
    LittleFS.failWrites=true;receiveChunk(2);LittleFS.failWrites=false;
    auto query=MeshTestProbe::frame(m,BITMAP_QUERY,800,9,g_nodeId,nullptr,0);MeshTestProbe::receive(m,query);
    reply=MeshTestProbe::take(m,BITMAP_ACK,false);memcpy(&a,reply.data()+sizeof(PacketHeader),sizeof(a));assert(a.bitmap==3 && !a.flags);
    StorageManager reboot;assert(reboot.begin());Record img;assert(reboot.get(IMAGE,800,img)&&img.bitmap==3);
    // CRC failure is reported by the next bitmap, even when no individual chunk ACK exists.
    receiveChunk(2,true);for(int k=3;k<imageChunks(info.bytes);k++)receiveChunk(k);
    MeshTestProbe::receive(m,MeshTestProbe::frame(m,BITMAP_QUERY,800,9,g_nodeId,nullptr,0));
    reply=MeshTestProbe::take(m,BITMAP_ACK,false);memcpy(&a,reply.data()+sizeof(PacketHeader),sizeof(a));assert(!a.bitmap && !a.flags);
    for(int k=0;k<imageChunks(info.bytes);k++)receiveChunk(k);
    assert(s.get(IMAGE,800,img)&&img.complete&&s.verifyImage(800,info,true));
    MeshTestProbe::receive(m,MeshTestProbe::frame(m,BITMAP_QUERY,800,9,g_nodeId,nullptr,0));
    reply=MeshTestProbe::take(m,BITMAP_ACK,false);memcpy(&a,reply.data()+sizeof(PacketHeader),sizeof(a));assert(a.bitmap==completeMask(info.bytes)&&a.flags==ACK_COMPLETE);
    // A receiver reboot/lost final ACK still returns completion; foreign sources get no receipt.
    LoRaMeshManager restored(&reboot);assert(restored.begin());
    MeshTestProbe::receive(restored,MeshTestProbe::frame(restored,BITMAP_QUERY,800,10,g_nodeId,nullptr,0));assert(!MeshTestProbe::queued(restored,BITMAP_ACK));
    MeshTestProbe::receive(restored,MeshTestProbe::frame(restored,BITMAP_QUERY,800,9,g_nodeId,nullptr,0));
    reply=MeshTestProbe::take(restored,BITMAP_ACK,false);memcpy(&a,reply.data()+sizeof(PacketHeader),sizeof(a));assert(a.flags==ACK_COMPLETE);
#endif
#endif
}
void adaptiveRecoveryTests(){
#if TRANSFER_MODE == TRANSFER_ADAPTIVE && CURRENT_NODE_ROLE == ROLE_CIVILIAN
    LittleFS.reset();StorageManager s;assert(s.begin());LoRaMeshManager m(&s);assert(m.begin());
    auto bytes=jpeg(180*10+37);ImagePayload info{850,(uint32_t)bytes.size(),crc32(bytes.data(),bytes.size())};
    Record report=sos(850);report.target=9;report.delivery=DELIVERY_RECEIVED;assert(s.save(report));
    Record img;img.kind=IMAGE;img.id=img.link=850;img.source=g_nodeId;img.target=9;img.complete=1;
    img.length=sizeof(info);memcpy(img.payload,&info,sizeof(info));assert(s.save(img));
    File f=LittleFS.open(s.imagePath(850),"w");assert(f.write(bytes.data(),bytes.size())==bytes.size());f.close();
    int i=s.find(IMAGE,850);MeshTestProbe::plan(m);auto manifest=MeshTestProbe::take(m,IMAGE);
    auto answer=[&](uint64_t req,uint64_t mask,uint8_t flags=0){
        BitmapAckPayload a{req,mask,flags};MeshTestProbe::receive(m,MeshTestProbe::frame(m,BITMAP_ACK,850,9,g_nodeId,&a,sizeof(a)));
    };
    fakeMillis+=800;answer(MeshTestProbe::header(manifest).packetId,0);
    auto& g=MeshTestProbe::group(m,850);assert(!g.ratePolicy.measured && g.window==2);
    uint64_t received=0;int rounds=0;
    while(received!=completeMask(info.bytes)) {
        assert(++rounds<12);MeshTestProbe::duePlan(m,i);
        const uint64_t batch=g.batchMask;const uint32_t started=g.groupStarted;
        const uint8_t used=g.window;int chunks=0;
        while(MeshTestProbe::queued(m,GROUP_CHUNK)) {
            assert(++chunks<=ADAPTIVE_MAX_CHUNKS);MeshTestProbe::take(m,GROUP_CHUNK);MeshTestProbe::duePlan(m,i);
        }
        auto query=MeshTestProbe::take(m,BITMAP_QUERY);auto request=MeshTestProbe::header(query);
        if(rounds==1) {
            // Data arrived, but its bitmap reply was lost. Retrying must send
            // only a query, retain the group size, and retain the original clock.
            MeshTestProbe::duePlan(m,i);assert(g.window==used && g.groupStarted==started);
            assert(!MeshTestProbe::queued(m,GROUP_CHUNK));
            auto retry=MeshTestProbe::take(m,BITMAP_QUERY);
            answer(request.packetId,batch);assert(s.get(IMAGE,850,img)&&!img.bitmap);
            request=MeshTestProbe::header(retry);
        }
        received|=batch;fakeMillis+=700;
        const uint32_t elapsed=fakeMillis-started;
        answer(request.packetId,received,received==completeMask(info.bytes) ? ACK_COMPLETE : 0);
        assert(g.unanswered==0 && !g.measuring);
        if(rounds==1) {
            assert(g.ratePolicy.preferred==2 && g.window==4);
            assert(g.ratePolicy.preferredRate==uint32_t(uint64_t(360)*1000/elapsed));
        }
        // Repeated delivery of the same reply must not double-count its bytes.
        const uint32_t rate=g.ratePolicy.preferredRate;const uint8_t next=g.window;
        answer(request.packetId,received,received==completeMask(info.bytes) ? ACK_COMPLETE : 0);
        assert(g.ratePolicy.preferredRate==rate && g.window==next);
    }
    assert(s.get(IMAGE,850,img)&&img.delivery==DELIVERY_RECEIVED&&img.bitmap==completeMask(info.bytes));
#endif
}
void clearTests(){
    auto file=[](const char* path){File f=LittleFS.open(path,"w");const uint8_t bytes[]={1,2,3};assert(f && f.write(bytes,3)==3);f.close();};
    LittleFS.reset();StorageManager s;assert(s.begin());LoRaMeshManager mesh(&s);assert(mesh.begin());
    for(size_t i=0;i<MAX_RECORDS;i++){auto r=sos(1000+i);assert(s.save(r));}
    file("/mesh3/photo.jpg");file("/mesh3/photo.part");file("/mesh3/bad.rec");file("/mesh3/write.rec.tmp");
    file("/unsent_sos.bin");file("/unrelated.txt");
    // Refusing to stop the radio must leave the records untouched.
    MeshTestProbe::radio(mesh).standbyResult=-1;
    assert(!mesh.clearStoredData());assert(s.count()==MAX_RECORDS);assert(!LittleFS.exists("/mesh3-clear.pending"));
    MeshTestProbe::radio(mesh).standbyResult=0;MeshTestProbe::seedClearState(mesh);
    assert(mesh.clearStoredData());MeshTestProbe::assertClearState(mesh);assert(mesh.ready());
    assert(s.ready() && !s.count() && !s.imageCount() && !s.unclaimed());
    assert(LittleFS.files.size()==1 && LittleFS.exists("/unrelated.txt"));
    StorageManager reboot;assert(reboot.begin() && !reboot.count());
    auto fresh=sos(2000);assert(s.save(fresh));assert(fresh.order==1);
    MeshTestProbe::plan(mesh);assert(MeshTestProbe::queued(mesh,SOS)==1); // New work still transmits.
    assert(mesh.clearStoredData());assert(mesh.clearStoredData()); // Repeated clear is safe.
    // Failure halfway through deletion blocks saves and remains recoverable after reboot.
    auto a=sos(3000),b=sos(3001);assert(s.save(a) && s.save(b));
    LittleFS.removesUntilFailure=1;
    assert(!mesh.clearStoredData());assert(!s.ready() && s.canClear());assert(!s.save(fresh));
    assert(LittleFS.exists("/mesh3-clear.pending"));MeshTestProbe::assertClearState(mesh);
    LittleFS.removesUntilFailure=-1;
    StorageManager afterPowerLoss;assert(afterPowerLoss.begin() && !afterPowerLoss.count());
    assert(!LittleFS.exists("/mesh3-clear.pending") && LittleFS.files.size()==1);
    // Failure to finish the marker removal is retryable without rebooting.
    assert(afterPowerLoss.save(fresh));LittleFS.failRemovePath="/mesh3-clear.pending";
    assert(!afterPowerLoss.clearAll() && !afterPowerLoss.ready());LittleFS.failRemovePath="";
    assert(afterPowerLoss.clearAll() && afterPowerLoss.ready());
    // Failure to create a marker must not erase the existing records.
    fresh=sos(4000);assert(afterPowerLoss.save(fresh));LittleFS.failWrites=true;
    assert(!afterPowerLoss.clearAll() && afterPowerLoss.count()==1);LittleFS.failWrites=false;
    // A successful clear stays successful if radio reception cannot restart.
    LoRaMeshManager failedRx(&afterPowerLoss);assert(failedRx.begin());MeshTestProbe::radio(failedRx).receiveResult=-1;
    assert(failedRx.clearStoredData() && !failedRx.ready() && afterPowerLoss.count()==0);
    LittleFS.reset();LittleFS.mount=false;StorageManager unmounted;assert(!unmounted.begin());assert(!unmounted.canClear() && !unmounted.clearAll());
}
void diagnosticsTests(){
    LittleFS.reset();StorageManager s;assert(s.begin());LoRaMeshManager m(&s);assert(m.begin());
    auto r=sos(9400,9);auto frame=MeshTestProbe::frame(m,SOS,r.id,9,0,r.payload,r.length);
    auto wrong=frame;wrong[2]=MESH_VERSION==3 ? 4 : 3;
    MeshTestProbe::receive(m,wrong);assert(m.versionRejectCount()==1 && m.authRejectCount()==0 && m.rxCount()==0);
    wrong=frame;wrong.back()^=1;MeshTestProbe::receive(m,wrong);
    assert(m.authRejectCount()==1 && m.invalidCount()==2 && m.rxCount()==0);
    LittleFS.failWrites=true;MeshTestProbe::receive(m,frame);LittleFS.failWrites=false;
    assert(m.sosStoreFailureCount()==1 && !MeshTestProbe::queued(m,ACK));
    frame=MeshTestProbe::frame(m,SOS,r.id,9,0,r.payload,r.length);MeshTestProbe::receive(m,frame);
    assert(s.count()==1);
    assert(MeshTestProbe::queued(m,ACK)==(CURRENT_NODE_ROLE==ROLE_RESCUER ? 1 : 0));
    m.update();fakeMillis+=1234;m.update();assert(m.maxServiceGapMs()>=1234);
}
void bufferedOutputTests(){
    struct Sink {
        std::string data;size_t calls=0,limit=512;bool fail=false;
        size_t write(const uint8_t* p,size_t n){++calls;if(fail)return 0;n=std::min(n,limit);data.append((const char*)p,n);return n;}
    } sink;
    uint8_t scratch[512];BufferedOutput<Sink> output(sink,scratch,sizeof(scratch));
    std::string expected;
    for(int i=0;i<4097;i++){uint8_t value=uint8_t(i%256);expected+=char(value);assert(output.write(value)==1);}
    assert(output.flush());assert(sink.data==expected && sink.calls==9);
    assert(output.flush() && sink.calls==9);
    Sink partial;partial.limit=7;BufferedOutput<Sink> small(partial,scratch,sizeof(scratch));
    assert(small.write((const uint8_t*)expected.data(),expected.size())==expected.size());
    assert(small.flush() && partial.data==expected);
    Sink failed;BufferedOutput<Sink> broken(failed,scratch,sizeof(scratch));
    assert(broken.write('x')==1);failed.fail=true;assert(!broken.flush());
    assert(!broken.write('y') && !broken.flush() && failed.calls==1);
    Sink empty;BufferedOutput<Sink> zero(empty,scratch,0);assert(!zero.write('x') && !zero.flush() && !empty.calls);
}
int main(){portalTests();protocolTests();persistenceTests();imageTests();legacyTests();meshTests();policyTests();groupedTests();adaptiveRecoveryTests();clearTests();diagnosticsTests();bufferedOutputTests();std::cout<<"PASS: production protocol/storage/radio, clear/restart/failure recovery, grouped transfer, diagnostics, buffered output, policy, portal (mode "<<TRANSFER_MODE<<", role "<<CURRENT_NODE_ROLE<<")\n";}
