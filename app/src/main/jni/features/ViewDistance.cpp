#include "ViewDistance.h"
#include "engine/Engine.h"
#include <algorithm>
#include <cmath>
#include <android/log.h>
#include <dlfcn.h>
#include <vector>

namespace features { namespace viewdistance {
namespace {
constexpr int minimum=4,maximum=32;
bool available=false;
const void* viewDistanceOption=nullptr;
std::vector<int> (*originalValues)(void*,const void*)=nullptr;
int (*originalMaximum)(void*)=nullptr;
int (*originalSet)(void*,const void*,int)=nullptr;
int (*originalSetFloat)(void*,const void*,float)=nullptr;
float (*originalMinimumProgress)(void*,const void*)=nullptr;
float (*originalMaximumProgress)(void*,const void*)=nullptr;
float (*originalProgress)(void*,const void*)=nullptr;
void (*originalLoad)(void*)=nullptr;
void* (*getOptions)(void*)=nullptr;
int (*getIntValue)(void*,const void*)=nullptr;
static_assert(sizeof(std::vector<int>)==12,"Options value list ABI");

int Clamp(int chunks) {return std::max(minimum,std::min(maximum,chunks));}
std::vector<int> Values(void* options,const void* option) {
    if(option!=viewDistanceOption) return originalValues(options,option);
    std::vector<int> values;
    values.reserve(maximum-minimum+1);
    for(int chunks=minimum;chunks<=maximum;++chunks) values.push_back(chunks);
    return values;
}
int Maximum(void* options) {
    // Options +76 selects the separate VR distance field. Keep its native caps.
    return engine::At<unsigned char>(options,76)?originalMaximum(options):maximum;
}
int Set(void* options,const void* option,int value) {
    // Preserve native observers: they request chunks and rebuild the renderer.
    return originalSet(options,option,option==viewDistanceOption?Clamp(value):value);
}
float ProgressMinimum(void* options,const void* option) {
    return option==viewDistanceOption?float(minimum):originalMinimumProgress(options,option);
}
float ProgressMaximum(void* options,const void* option) {
    return option==viewDistanceOption?float(maximum):originalMaximumProgress(options,option);
}
float Progress(void* options,const void* option) {
    return option==viewDistanceOption?float(Clamp(getIntValue(options,option))):originalProgress(options,option);
}
int SetFloat(void* options,const void* option,float value) {
    if(option!=viewDistanceOption) return originalSetFloat(options,option,value);
    if(!std::isfinite(value)) return 0;
    const float bounded=std::max(float(minimum),std::min(float(maximum),value));
    const int chunks=Clamp(int(::floorf(bounded+.5f)));
    // A continuous thumb still stores whole chunks. Avoid rebuilding chunks on
    // every pointer movement while the rounded value stays unchanged.
    if(chunks==getIntValue(options,option)) return 0;
    return originalSet(options,option,chunks);
}
void Load(void* options) {
    originalLoad(options);
    // The native loader writes fields directly. Normalize only normal distance
    // after loading; initialization must not fire renderer/network observers.
    int& chunks=engine::At<int>(options,100);
    chunks=Clamp(chunks);
}
const engine::Hook hooks[]={
    {"_ZN7Options9getValuesEPKNS_6OptionE",0x3d20dc,"794bf0b57b44794d",reinterpret_cast<void*>(Values),reinterpret_cast<void**>(&originalValues)},
    {"_ZNK7Options24getMaxViewDistanceChunksEv",0x38c7a4,"08b5fff739ff4368",reinterpret_cast<void*>(Maximum),reinterpret_cast<void**>(&originalMaximum)},
    {"_ZN7Options3setEPKNS_6OptionEi",0x38f984,"364b2de9f0470446",reinterpret_cast<void*>(Set),reinterpret_cast<void**>(&originalSet)},
    {"_ZN7Options14getProgressMinEPKNS_6OptionE",0x38f268,"134b144a7b449a58",reinterpret_cast<void*>(ProgressMinimum),reinterpret_cast<void**>(&originalMinimumProgress)},
    {"_ZN7Options14getProgressMaxEPKNS_6OptionE",0x38f1fc,"134b144a7b449a58",reinterpret_cast<void*>(ProgressMaximum),reinterpret_cast<void**>(&originalMaximumProgress)},
    {"_ZN7Options16getProgressValueEPKNS_6OptionE",0x38f824,"1e4b1f4a7b449a58",reinterpret_cast<void*>(Progress),reinterpret_cast<void**>(&originalProgress)},
    {"_ZN7Options3setEPKNS_6OptionEf",0x38fc94,"334b2de9f0470e46",reinterpret_cast<void*>(SetFloat),reinterpret_cast<void**>(&originalSetFloat)},
    {"_ZN7Options5_loadEv",0x3e8edc,"2de9f04f2ded028b",reinterpret_cast<void*>(Load),reinterpret_cast<void**>(&originalLoad)},
};
}
void InstallVerified(void* library) {
    viewDistanceOption=dlsym(library,"_ZN7Options6Option13VIEW_DISTANCEE");
    getOptions=reinterpret_cast<decltype(getOptions)>(dlsym(library,"_ZN15MinecraftClient10getOptionsEv"));
    getIntValue=reinterpret_cast<decltype(getIntValue)>(dlsym(library,"_ZN7Options11getIntValueEPKNS_6OptionE"));
    if(!viewDistanceOption || !getOptions || !getIntValue || !engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0]))) return;
    available=true;
    __android_log_print(ANDROID_LOG_INFO,"SunchViewDistance","Render distance unlocked: %d-%d chunks, step 1",minimum,maximum);
}
bool IsUnlockedOption(const void* option) {return available && option==viewDistanceOption;}
int GetChunks(void* client) {
    return available && client?Clamp(getIntValue(getOptions(client),viewDistanceOption)):minimum;
}
}}
