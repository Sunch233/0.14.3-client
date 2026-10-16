#include "GraphicsSettings.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace graphicssettings {
namespace {
struct Hash {
    std::uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                          0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::uint8_t block[64] = {};
    std::uint64_t bytes = 0;
    unsigned used = 0;
    static std::uint32_t R(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32-n)); }
    void Transform() {
        static const std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        std::uint32_t w[64];
        for (unsigned i=0;i<16;++i) {
            const auto* p = block+4*i;
            w[i]=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
        }
        for (unsigned i=16;i<64;++i) {
            auto x=w[i-15], y=w[i-2];
            w[i]=w[i-16]+(R(x,7)^R(x,18)^(x>>3))+w[i-7]+(R(y,17)^R(y,19)^(y>>10));
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for (unsigned i=0;i<64;++i) {
            auto t1=v+(R(e,6)^R(e,11)^R(e,25))+((e&f)^((~e)&g))+k[i]+w[i];
            auto t2=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    void Update(const void* data, std::size_t size) {
        auto* p=static_cast<const std::uint8_t*>(data);
        bytes += size;
        while (size) {
            auto n=std::min<std::size_t>(size,64-used);
            std::memcpy(block+used,p,n);used+=unsigned(n);p+=n;size-=n;
            if (used==64) {Transform();used=0;}
        }
    }
    std::string Finish() {
        const auto bits=bytes*8;
        const std::uint8_t one=0x80,zero=0;
        Update(&one,1);
        while (used!=56) Update(&zero,1);
        std::uint8_t len[8];
        for(unsigned i=0;i<8;++i) len[7-i]=std::uint8_t(bits>>(8*i));
        Update(len,8);
        char out[65];
        for(unsigned i=0;i<8;++i) ::snprintf(out+i*8,9,"%08x",unsigned(h[i]));
        return out;
    }
};
}
std::string Sha256(const void* data, std::size_t size) { Hash h;h.Update(data,size);return h.Finish(); }
bool Sha256File(const char* path, std::string& digest) {
    digest.clear();
    FILE* f=std::fopen(path,"rb");
    if (!f) return false;
    Hash hash;
    std::uint8_t buffer[16384];
    std::size_t n;
    while ((n=std::fread(buffer,1,sizeof(buffer),f))!=0) hash.Update(buffer,n);
    bool ok=!std::ferror(f);
    std::fclose(f);
    if(ok) digest=hash.Finish();
    return ok;
}
} // namespace graphicssettings
