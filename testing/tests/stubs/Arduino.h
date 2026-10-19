#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <memory>
#include <vector>
#include <map>
#include <cmath>
using byte=uint8_t;
#define IRAM_ATTR
#define OUTPUT 1
#define LOW 0
#define HIGH 1
// Match Arduino's function-like macro so host builds catch name collisions.
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
inline uint32_t fakeMillis=100;
inline uint32_t millis(){return fakeMillis;}
inline void delay(unsigned n){fakeMillis+=n;}
inline uint32_t esp_random(){static uint32_t x=1327;x^=x<<13;x^=x>>17;x^=x<<5;return x;}
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
class String:public std::string {
public:
    using std::string::string;
    String(const std::string& s):std::string(s){}
    String(unsigned n):std::string(std::to_string(n)){}
    String(int n):std::string(std::to_string(n)){}
    bool endsWith(const char* s)const {size_t n=strlen(s);return size()>=n && compare(size()-n,n,s)==0;}
};
struct SerialStub {template<class... T> void printf(const char*,T...){} template<class T> void println(T){} };
inline SerialStub Serial;
