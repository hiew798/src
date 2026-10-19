#pragma once
// Test double only: tests frame-tag rejection/routing, not SHA-256 cryptography.
#include <cstdint>
#include <cstddef>
#define MBEDTLS_MD_SHA256 1
inline const void* mbedtls_md_info_from_type(int){return nullptr;}
inline int mbedtls_md_hmac(const void*,const unsigned char* k,size_t kn,const unsigned char* d,size_t n,unsigned char* out){
    uint32_t x=2166136261u;for(size_t i=0;i<kn;i++)x=(x^k[i])*16777619u;for(size_t i=0;i<n;i++)x=(x^d[i])*16777619u;
    for(int i=0;i<32;i++){x^=x<<13;x^=x>>17;x^=x<<5;out[i]=uint8_t(x);}return 0;
}
