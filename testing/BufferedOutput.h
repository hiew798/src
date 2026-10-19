#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ArduinoJson writes individual punctuation bytes. Batch them before they reach
// an unbuffered HTTP/TLS connection, using scratch space owned by the server.
template<class Destination> class BufferedOutput {
    Destination& destination;
    uint8_t* buffer;
    size_t capacity, used=0;
    bool failed=false;
public:
    BufferedOutput(Destination& out,uint8_t* scratch,size_t size)
        : destination(out),buffer(scratch),capacity(size) { failed=size==0; }
    bool flush() {
        if(failed) return false;
        size_t sent=0;
        while(sent<used) {
            size_t n=destination.write(buffer+sent,used-sent);
            if(!n || n>used-sent) { failed=true; used=0; return false; }
            sent+=n;
        }
        used=0; return true;
    }
    size_t write(uint8_t value) { return write(&value,1); }
    size_t write(const uint8_t* data,size_t size) {
        size_t accepted=0;
        while(!failed && accepted<size) {
            if(used==capacity && !flush()) break;
            size_t n=size-accepted;
            if(n>capacity-used) n=capacity-used;
            memcpy(buffer+used,data+accepted,n); used+=n; accepted+=n;
        }
        return accepted;
    }
};
