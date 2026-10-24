#include "Hud.h"
#include "FpsCounter.h"
#include "TargetInfo.h"
#include "VSync.h"
#include "engine/Engine.h"
#include "settings/GraphicsSettings.h"
#include <android/log.h>
#include <dlfcn.h>

namespace features { namespace hud {
namespace {
using engine::Color;
void (*originalFrame)(void*,float)=nullptr;
void (*originalHudRender)(void*,void*,void*,int)=nullptr;
void* (*getFont)(void*)=nullptr;
void (*drawText)(void*,const std::string&,float,float,const Color&,bool)=nullptr;
bool targetInfoEnabled=false;

void Frame(void* self,float dt) {
    const auto settings=graphicssettings::Get();
    vsync::Apply(settings.vsync);
    targetInfoEnabled=settings.blockInfo;
    fpscounter::BeginFrame(settings.showFps);
    originalFrame(self,dt);
}
void HudRender(void* self,void* client,void* control,int pass) {
    originalHudRender(self,client,control,pass);
    void* font=getFont(client);
    if(!font) return;
    if(fpscounter::Enabled()) drawText(font,fpscounter::Text(),4.f,4.f,Color{1.f,1.f,1.f,1.f},false);
    if(targetInfoEnabled) targetinfo::Render(client,font);
}
const engine::Hook hooks[]={
    {"_ZN12GameRenderer6renderEf",0x46e888,"2de9f04f2ded108b",reinterpret_cast<void*>(Frame),reinterpret_cast<void**>(&originalFrame)},
    {"_ZN23HudDebugOverlayRenderer6renderER15MinecraftClientRSt10shared_ptrI9UIControlEi",0x2e26e8,"10b508460c46fff7",reinterpret_cast<void*>(HudRender),reinterpret_cast<void**>(&originalHudRender)},
};
}
void InstallVerified(void* lib) {
    getFont=reinterpret_cast<decltype(getFont)>(dlsym(lib,"_ZNK15MinecraftClient7getFontEv"));
    drawText=reinterpret_cast<decltype(drawText)>(dlsym(lib,"_ZN4Font10drawShadowERKSsffRK5Colorb"));
    if(!getFont || !drawText) return;
    if(engine::InstallHooks(lib,hooks,sizeof(hooks)/sizeof(hooks[0])))
        __android_log_print(ANDROID_LOG_INFO,"SunchFeatures","HUD and frame hooks installed");
}
}}
