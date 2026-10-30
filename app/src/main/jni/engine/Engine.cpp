#include "Engine.h"
#include "Substrate/CydiaSubstrate.h"
#include <android/log.h>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace engine {
bool VerifyBinary(void* lib) {
    static_assert(sizeof(void*)==4 && sizeof(std::string)==4,"Verified ARMv7 GNU STL ABI");
    Dl_info image{};
    void* anchor=dlsym(lib,"_ZN12GameRenderer6renderEf");
    if(!anchor || !dladdr(anchor,&image)) return false;
    std::string hash;
    if(!image.dli_fname || !Sha256File(image.dli_fname,hash) ||
       hash!="ef26c33fc8a1f554ab3e4ed0bf39bd656f376f73706f52f9a3e6bd21989ef402") {
        __android_log_print(ANDROID_LOG_ERROR,"SunchEngine","Unsupported engine binary");
        return false;
    }
    return true;
}

bool InstallHooks(void* lib,const Hook* hooks,std::size_t count) {
    Dl_info engineImage{};
    void* anchor=dlsym(lib,"_ZN12GameRenderer6renderEf");
    if(!anchor || !dladdr(anchor,&engineImage)) return false;
    std::vector<void*> targets(count);
    for(std::size_t i=0;i<count;++i) {
        const auto& hook=hooks[i];
        targets[i]=hook.byRva
            ?reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(engineImage.dli_fbase)+hook.rva)|1u)
            :dlsym(lib,hook.name);
        const uintptr_t address=reinterpret_cast<uintptr_t>(targets[i]);
        Dl_info image{};
        unsigned char expected[8];
        for(unsigned j=0;j<8;++j) {
            unsigned byte=0;
            std::sscanf(hook.prefix+j*2,"%2x",&byte);
            expected[j]=byte;
        }
        if(!(address&1) || !dladdr(targets[i],&image) ||
           (address&~uintptr_t(1))-reinterpret_cast<uintptr_t>(image.dli_fbase)!=hook.rva ||
           std::memcmp(reinterpret_cast<void*>(address&~uintptr_t(1)),expected,8)) {
            __android_log_print(ANDROID_LOG_WARN,"SunchEngine","Hook entrypoint mismatch: %s",hook.name);
            return false;
        }
    }
    for(std::size_t i=0;i<count;++i)
        MSHookFunction(targets[i],hooks[i].replace,hooks[i].original);
    return true;
}
}
