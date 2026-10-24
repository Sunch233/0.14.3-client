#include "DynamicLight.h"
#include "engine/Engine.h"
#include "settings/GraphicsSettings.h"
class TextureUVCoordinateSet;
#include "mcpe/MinecraftGame.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <string>

namespace features { namespace dynamiclight {
namespace {
struct Vec3 {float x,y,z;};
struct Float4 {float x=0,y=0,z=0,w=0;};
static_assert(sizeof(void*)==4 && sizeof(std::string)==4,"Verified ARMv7 GNU STL ABI required");
void** managerSlot=nullptr;
unsigned char* blockEmission=nullptr;
void* (*findBuffer)(void*,const std::string&)=nullptr;
void* (*findConstant)(void*,const std::string&)=nullptr;
int (*constantSize)(void*)=nullptr;
void* (*getLocalPlayer)(void*)=nullptr;
void* (*getSelectedItem)(const void*)=nullptr;
int (*itemId)(const void*)=nullptr;
int (*itemAux)(const void*)=nullptr;
void (*interpolatedPosition)(Vec3*,const void*,float)=nullptr;
using WorldMethod=void(*)(void*,void*,void*,void*,float,float);
WorldMethod originalWorld=nullptr;
bool available=false;

bool Submit(const Float4& origin,const Float4& control) {
    if(!managerSlot || !*managerSlot) return false;
    void* manager=*managerSlot;
    auto* range=static_cast<const uintptr_t*>(manager);
    if(!range[0] || range[1]<range[0] || range[1]-range[0]>44*256 || (range[1]-range[0])%44) return false;
    static const std::string group="SunchDynamicLightConstants";
    void* buffer=findBuffer(manager,group);
    const auto b=reinterpret_cast<uintptr_t>(buffer);
    // A failed native buffer lookup returns vector::end(), not nullptr.
    if(b<range[0] || b>=range[1] || (b-range[0])%44) return false;
    auto* storage=reinterpret_cast<const uintptr_t*>(b+24);
    const std::string names[]={"SUNCH_LIGHT_ORIGIN","SUNCH_LIGHT_CONTROL"};
    void* constants[2]{};
    float* values[2]{};
    for(unsigned i=0;i<2;++i) {
        auto* c=static_cast<unsigned char*>(findConstant(buffer,names[i]));
        if(!c || c[16]!=4 || constantSize(c)!=16) return false;
        auto address=*reinterpret_cast<uintptr_t*>(c+20);
        if(storage[1]<storage[0] || address<storage[0] || address>storage[1] || storage[1]-address<16 || address%4) return false;
        constants[i]=c;values[i]=reinterpret_cast<float*>(address);
    }
    const Float4 data[]={origin,control};
    for(unsigned i=0;i<2;++i) {
        if(std::memcmp(values[i],&data[i],16)) {
            std::memcpy(values[i],&data[i],16);
            static_cast<unsigned char*>(constants[i])[17]=1;
        }
    }
    return true;
}
void World(void* self,void* camera,void* frustum,void* other,float dt,float partialTick) {
    Float4 origin,control;
    if(graphicssettings::Get().dynamicLight) {
        auto* client=MinecraftClient::getInstance();
        void* player=client?getLocalPlayer(client):nullptr;
        void* item=player?getSelectedItem(player):nullptr;
        if(item && *static_cast<unsigned char*>(item)>0) {
            const int id=itemId(item);
            int emission=(id>0 && id<256)?blockEmission[id]:0;
            // PE 0.14 uses one bucket item with the contained liquid in aux.
            if(id==325 && itemAux(item)==10) emission=15;
            emission=std::max(0,std::min(15,emission));
            if(emission) {
                Vec3 position{};
                const float tick=std::isfinite(partialTick)?std::max(0.f,std::min(1.f,partialTick)):0.f;
                interpolatedPosition(&position,player,tick);
                if(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
                    // Player position is eye-height in this client. Place the light by the hand.
                    origin={position.x,position.y-.35f,position.z,1.f};
                    control.x=float(emission);
                }
            }
        }
    }
    const bool submitted=Submit(origin,control);
    static int lastEmission=-1,lastStatus=-1;
    if(lastEmission!=int(control.x) || lastStatus!=int(submitted)) {
        __android_log_print(ANDROID_LOG_INFO,"SunchDynamicLight","held emission=%d uniforms=%s",int(control.x),submitted?"ready":"pending");
        lastEmission=int(control.x);lastStatus=int(submitted);
    }
    originalWorld(self,camera,frustum,other,dt,partialTick);
    // Keep GUI previews and other later render passes free of scene lighting.
    Submit(Float4{},Float4{});
}
const engine::Hook hooks[]={
    {"_ZN13LevelRenderer11renderLevelER6EntityR13FrustumCullerS3_ff",0x49bdb8,"10b49ded010added",reinterpret_cast<void*>(World),reinterpret_cast<void**>(&originalWorld)},
};
}
bool Available() {return available;}
void InstallVerified(void* lib) {
    bool ok=true;
#define S(name,value) value=reinterpret_cast<decltype(value)>(dlsym(lib,name));ok=ok && value
    S("_ZN3mce9SingletonINS_27GlobalConstantBufferManagerESt10unique_ptrIS1_St14default_deleteIS1_EEE8instanceE",managerSlot);
    S("_ZN3mce27GlobalConstantBufferManager27findConstantBufferContainerERKSs",findBuffer);
    S("_ZN3mce23ConstantBufferContainer30getUnspecializedShaderConstantERKSs",findConstant);
    S("_ZNK3mce18ShaderConstantBase15getConstantSizeEv",constantSize);
    S("_ZN5Block14mLightEmissionE",blockEmission);
    S("_ZN15MinecraftClient14getLocalPlayerEv",getLocalPlayer);
    S("_ZNK6Player15getSelectedItemEv",getSelectedItem);
    S("_ZNK12ItemInstance5getIdEv",itemId);
    S("_ZNK12ItemInstance11getAuxValueEv",itemAux);
    S("_ZNK6Entity23getInterpolatedPositionEf",interpolatedPosition);
#undef S
    if(!ok) {__android_log_print(ANDROID_LOG_WARN,"SunchDynamicLight","Missing engine helpers");return;}
    if(engine::InstallHooks(lib,hooks,sizeof(hooks)/sizeof(hooks[0])))
        available=originalWorld!=nullptr;
}
}
} // namespace features
