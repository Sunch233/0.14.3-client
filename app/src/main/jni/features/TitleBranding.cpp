#include "TitleBranding.h"
#include "engine/Engine.h"
#include "settings/InformationContent.h"
#include <android/log.h>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <string>

namespace features { namespace titlebranding {
namespace {
using engine::Color;
using Draw=void(*)(void*,const std::string&,float,float,const Color&,bool);
Draw originalDraw=nullptr,originalShadow=nullptr;
int (*lineLength)(void*,const std::string&,float,bool)=nullptr;
thread_local bool insideDraw=false;
Color Rainbow(float hue,float alpha) {
    const float h=(hue-std::floor(hue)) * 6.f;
    const int sector=int(h);
    const float f=h-float(sector);
    const float low=.2f,fall=1.f-.8f*f,rise=.2f+.8f*f;
    switch(sector) {
        case 0:return {1.f,rise,low,alpha};
        case 1:return {fall,1.f,low,alpha};
        case 2:return {low,1.f,rise,alpha};
        case 3:return {low,fall,1.f,alpha};
        case 4:return {rise,low,1.f,alpha};
        default:return {1.f,low,fall,alpha};
    }
}
void DrawBranding(void* font,float x,float copyrightY,float alpha,bool flag) {
    static const std::string label=std::string("Sunch Client ")+informationcontent::version;
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float phase=float(std::fmod(seconds,5.0)/5.0);
    std::string prefix;
    for(std::size_t offset=0;offset<label.size();) {
        std::size_t next=offset+1;
        while(next<label.size() && (static_cast<unsigned char>(label[next])&0xc0)==0x80) ++next;
        const std::string glyph=label.substr(offset,next-offset);
        const float advance=float(lineLength(font,prefix,1.f,false));
        const Color tint=Rainbow(phase+.75f*float(offset)/float(label.size()),alpha);
        originalShadow(font,glyph,x+advance,copyrightY-11.f,tint,flag);
        prefix+=glyph;
        offset=next;
    }
}
void Render(Draw original,void* font,const std::string& text,float x,float y,const Color& color,bool flag) {
    if(insideDraw) {original(font,text,x,y,color,flag);return;}
    insideDraw=true;
    // Anchor to the actual title copyright; this follows GUI scaling and margins.
    if(text==u8"©Mojang AB") DrawBranding(font,x,y,color.a,flag);
    original(font,text,x,y,color,flag);
    insideDraw=false;
}
void Plain(void* f,const std::string& t,float x,float y,const Color& c,bool flag) {Render(originalDraw,f,t,x,y,c,flag);}
void Shadow(void* f,const std::string& t,float x,float y,const Color& c,bool flag) {Render(originalShadow,f,t,x,y,c,flag);}
const engine::Hook hooks[]={
    {"_ZN4Font4drawERKSsffRK5Colorb",0x34100c,"70b584b00026089c",reinterpret_cast<void*>(Plain),reinterpret_cast<void**>(&originalDraw)},
    {"_ZN4Font10drawShadowERKSsffRK5Colorb",0x345708,"2de9f04f2ded028b",reinterpret_cast<void*>(Shadow),reinterpret_cast<void**>(&originalShadow)},
};
}
void InstallVerified(void* lib) {
    static_assert(sizeof(void*)==4 && sizeof(std::string)==4,"Verified ARMv7 GNU STL ABI");
    lineLength=reinterpret_cast<decltype(lineLength)>(dlsym(lib,"_ZNK4Font13getLineLengthERKSsfb"));
    if(!lineLength) return;
    if(engine::InstallHooks(lib,hooks,sizeof(hooks)/sizeof(hooks[0])))
        __android_log_print(ANDROID_LOG_INFO,"SunchBranding","Title branding installed: version %s",informationcontent::version);
}
}
} // namespace features
