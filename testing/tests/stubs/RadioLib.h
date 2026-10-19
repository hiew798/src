#pragma once
#include "Arduino.h"
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_RX_TIMEOUT -6
#define RADIOLIB_ERR_CRC_MISMATCH -7
#define RADIOLIB_CHANNEL_FREE 1
#define RADIOLIB_SX126X_IRQ_TX_DONE 0x01
#define RADIOLIB_SX126X_IRQ_RX_DONE 0x02
#define RADIOLIB_SX126X_IRQ_HEADER_VALID 0x10
#define RADIOLIB_SX126X_IRQ_HEADER_ERR 0x20
#define RADIOLIB_SX126X_IRQ_CRC_ERR 0x40
#define RADIOLIB_SX126X_IRQ_TIMEOUT 0x200
struct Module{Module(int,int,int,int){}};
class SX1262 {
    void(*callback)()=nullptr;
public:
    uint32_t flags=0;
    int standbyResult=0,receiveResult=0,standbyCalls=0;
    uint32_t getIrqFlags(){return flags;}
    SX1262(Module*){}
    int begin(double,double,int,int,int,int,int){return 0;}
    void setDio1Action(void(*f)()){callback=f;}
    void clearDio1Action(){callback=nullptr;}
    int standby(){++standbyCalls;flags=0;return standbyResult;}
    int startReceive(){flags=0;return receiveResult;} int finishTransmit(){return 0;}
    size_t getPacketLength(){return 0;} int readData(uint8_t*,size_t){return 0;}
    float getRSSI(){return -70;} int scanChannel(){return RADIOLIB_CHANNEL_FREE;}
    int startTransmit(uint8_t*,size_t){return 0;}
};
