#include "GraphicsSettings.h"
#include <android/log.h>
#include "Substrate/CydiaSubstrate.h"
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <memory>
#include <vector>

namespace graphicssettings {
namespace {
// MCPE 0.14.3 ARMv7 / GNU STL. All object sizes and entrypoints verified in IDA.
struct Option {int type;std::string key;};
static_assert(sizeof(Option)==8 && sizeof(std::shared_ptr<void>)==8 && sizeof(std::vector<std::shared_ptr<void>>)==12,"Options ABI");
Option fpsOption{0,"帧数显示"};
std::string Number(int value) {char buffer[32];snprintf(buffer,sizeof(buffer),"%d",value);return buffer;}
bool Custom(const Option* p) {return p==&fpsOption;}
template<class T> T& At(void* p,unsigned offset) {return *reinterpret_cast<T*>(static_cast<char*>(p)+offset);}
using PaneList=std::vector<std::shared_ptr<void>>;
void (*originalGenerate)(void*)=nullptr;
void (*originalButtons)(void*)=nullptr;
void (*originalScreenRender)(void*,int,int,float)=nullptr;
void (*originalHudRender)(void*,void*,void*,int)=nullptr;
bool (*originalBoolean)(void*,const Option*)=nullptr;
bool (*originalCanModify)(void*,const Option*)=nullptr;
void (*originalToggle)(void*,const Option*,int)=nullptr;
void (*paneConstructor)(void*)=nullptr;
void (*containerConstructor)(void*,bool,bool,int,int,int,int)=nullptr;
void* groupVtable=nullptr;
void (*containerRender)(void*,void*,int,int)=nullptr;
void (*originalItemRender)(void*,void*,int,int)=nullptr;
bool (*isSelected)(void*)=nullptr;
bool (*isLocked)(void*)=nullptr;
void* (*addOption)(void*,const Option&,void*)=nullptr;
struct Color {float r,g,b,a;};
void* (*getFont)(void*)=nullptr;
void (*drawText)(void*,const std::string&,float,float,const Color&,bool)=nullptr;
int (*createButton)(void*,int,int,int,int,int,int)=nullptr;

// Render-thread state. Count engine frames, not HUD passes or simulation ticks.
using FpsClock=std::chrono::steady_clock;
FpsClock::time_point fpsStart{},fpsPrevious{};
unsigned fpsFrames=0;
bool fpsEnabled=false,fpsSampling=false;
std::string fpsText="FPS: --";
void HudRender(void* self,void* client,void* control,int pass) {
    originalHudRender(self,client,control,pass);
    if(!fpsEnabled) return;
    void* font=getFont(client);
    if(font) drawText(font,fpsText,4.f,4.f,Color{1.f,1.f,1.f,1.f},false);
}

void DeleteElement(void* p) {auto vt=*static_cast<void***>(p);reinterpret_cast<void(*)(void*)>(vt[1])(p);}
bool Advanced(void* group) {return At<std::string>(group,68)=="高级图形";}
void ScreenRender(void* screen,int x,int y,float dt) {
    originalScreenRender(screen,x,y,dt);
    if(At<int>(screen,228)==1) return; // Skin-only screen has no category strip.
    const auto& panes=At<PaneList>(screen,140);const auto& buttons=At<PaneList>(screen,200);
    if(panes.empty() || panes.size()!=buttons.size()) return;
    const auto& groups=At<PaneList>(panes.back().get(),48);
    if(groups.empty() || !Advanced(groups[0].get())) return;
    void* button=buttons.back().get();const Color gold{1.f,.85f,.35f,1.f};
    drawText(getFont(At<void*>(screen,20)),"+",float(At<int>(button,8)+19),float(At<int>(button,12)+17),gold,false);
}

void Buttons(void* screen) {
    originalButtons(screen);
    auto& buttons=At<PaneList>(screen,200);
    // ID starts at 2, selection is ID - 2. Append without changing vanilla IDs.
    createButton(screen,int(buttons.size())+2,At<int>(screen,164),134,28,0,0);
}
void Generate(void* screen) {
    originalGenerate(screen);
    auto& panes=At<PaneList>(screen,140);
    const auto& buttons=At<PaneList>(screen,200);
    if(panes.size()<3 || panes.size()>5 || buttons.size()!=panes.size()+1) return;
    void* raw=::operator new(0x70);paneConstructor(raw);
    // The game destroys the concrete OptionsPane through its original virtual D0.
    std::shared_ptr<void> pane(raw,DeleteElement);
    void* client=At<void*>(screen,20);
    // OptionsGroup's original constructor translates its title. Construct the
    // verified 72-byte layout directly so this page never enters translation.
    void* group=::operator new(0x48);
    containerConstructor(group,false,true,0,0,24,24);
    *static_cast<void**>(group)=static_cast<char*>(groupVtable)+8;
    new(static_cast<char*>(group)+68) std::string("高级图形");
    At<PaneList>(raw,48).push_back(std::shared_ptr<void>(group,DeleteElement));
    addOption(group,fpsOption,client);
    panes.push_back(std::move(pane));
}
bool Boolean(void* self,const Option* p) {
    return Custom(p)?Get().showFps:originalBoolean(self,p);
}
bool CanModify(void* self,const Option* p) {
    return Custom(p)?true:originalCanModify(self,p);
}
void Toggle(void* self,const Option* p,int direction) {
    if(Custom(p)) {SetShowFps(!Get().showFps);return;}
    originalToggle(self,p,direction);
}
void (*originalFrame)(void*,float)=nullptr;
void Frame(void* self,float dt) {
    BeginHudFrame();
    originalFrame(self,dt);
}
void ItemRender(void* item,void* client,int x,int y) {
    const Option* option=At<const Option*>(item,68);
    if(!Custom(option)) {originalItemRender(item,client,x,y);return;}
    Color color=isSelected(item)?Color{1.f,1.f,.627f,1.f}:Color{.733f,.733f,.733f,1.f};
    if(isLocked(item)) {color.r*=.5f;color.g*=.5f;color.b*=.5f;color.a=.75f;}
    drawText(getFont(client),option->key,
        float(At<int>(item,8)),float(At<int>(item,12)+(At<int>(item,20)-8)/2),color,false);
    containerRender(item,client,x,y);
}
struct Hook {const char* name;uintptr_t rva;const char* prefix;void* replace;void** original;};
#define O(name,rva,prefix,fn,original) {name,rva,prefix,reinterpret_cast<void*>(fn),reinterpret_cast<void**>(&original)}
const Hook hooks[]={
    O("_ZN12GameRenderer6renderEf",0x46e888,"2de9f04f2ded108b",Frame,originalFrame),
    O("_ZN23HudDebugOverlayRenderer6renderER15MinecraftClientRSt10shared_ptrI9UIControlEi",0x2e26e8,"10b508460c46fff7",HudRender,originalHudRender),
    O("_ZN11OptionsItem6renderEP15MinecraftClientii",0x3466bc,"2de9f04f2ded028b",ItemRender,originalItemRender),
    O("_ZN13OptionsScreen29_generateOptionScreensDefaultEv",0x3f0c44,"2de9f04f95b00446",Generate,originalGenerate),
    O("_ZN13OptionsScreen21createCategoryButtonsEv",0x3efeb0,"2de9f04104468eb0",Buttons,originalButtons),
    O("_ZN13OptionsScreen6renderEiif",0x3757f8,"2de9f04f044691b0",ScreenRender,originalScreenRender),
    O("_ZN7Options15getBooleanValueEPKNS_6OptionE",0x38f2d4,"c84bc94a7b4410b5",Boolean,originalBoolean),
    O("_ZN7Options6toggleEPKNS_6OptionEi",0x3e78c4,"2de9f84f8046dff8",Toggle,originalToggle),
    O("_ZN7Options9canModifyEPKNS_6OptionE",0x38f1ac,"10b5046c406c8442",CanModify,originalCanModify),
};
#undef O
}
void BeginHudFrame() {
    fpsEnabled=Get().showFps;
    if(!fpsEnabled) {fpsSampling=false;return;}
    const auto now=FpsClock::now();
    if(!fpsSampling ||
       std::chrono::duration<double>(now-fpsPrevious).count()>1.0) {
        fpsSampling=true;fpsStart=now;fpsPrevious=now;fpsFrames=0;
        fpsText="FPS: --";return;
    }
    fpsPrevious=now;
    ++fpsFrames;
    const double elapsed=std::chrono::duration<double>(now-fpsStart).count();
    if(elapsed>=.5) {
        fpsText="FPS: "+Number(int(fpsFrames/elapsed+.5));
        fpsStart=now;fpsFrames=0;
    }
}
void InstallVerified(void* lib) {
    // Reject any other engine build before using the verified ARMv7 layouts.
    Dl_info image{};void* anchor=dlsym(lib,hooks[0].name);
    if(!anchor || !dladdr(anchor,&image)) return;
    std::string hash;
    if(!image.dli_fname || !Sha256File(image.dli_fname,hash) ||
       hash!="ef26c33fc8a1f554ab3e4ed0bf39bd656f376f73706f52f9a3e6bd21989ef402") {
        __android_log_print(ANDROID_LOG_ERROR,"ClientSettings","Unsupported engine binary");
        return;
    }
    void* targets[sizeof(hooks)/sizeof(hooks[0])]{};
    for(unsigned i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i) {
        const auto& h=hooks[i];targets[i]=dlsym(lib,h.name);
        const uintptr_t a=reinterpret_cast<uintptr_t>(targets[i]);
        unsigned char expected[8];for(unsigned j=0;j<8;++j) {unsigned b=0;std::sscanf(h.prefix+j*2,"%2x",&b);expected[j]=b;}
        if(!(a&1) || (a&~uintptr_t(1))-reinterpret_cast<uintptr_t>(image.dli_fbase)!=h.rva ||
           std::memcmp(reinterpret_cast<void*>(a&~uintptr_t(1)),expected,8)) return;
    }
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(lib,name));ok=ok && target
    S("_ZN11OptionsPaneC2Ev",paneConstructor);
    S("_ZN19GuiElementContainerC2Ebbiiii",containerConstructor);
    S("_ZTV12OptionsGroup",groupVtable);
    S("_ZN19GuiElementContainer6renderEP15MinecraftClientii",containerRender);
    S("_ZN10GuiElement10isSelectedEv",isSelected);
    S("_ZN11OptionsItem9_isLockedEv",isLocked);
    S("_ZN12OptionsGroup13addOptionItemERKN7Options6OptionER15MinecraftClient",addOption);
    S("_ZNK15MinecraftClient7getFontEv",getFont);
    S("_ZN4Font10drawShadowERKSsffRK5Colorb",drawText);
    S("_ZN13OptionsScreen20createCategoryButtonEiiiiii",createButton);
#undef S
    if(!ok) return;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)
        MSHookFunction(targets[i],hooks[i].replace,hooks[i].original);
    __android_log_print(ANDROID_LOG_INFO,"ClientSettings","Native advanced graphics hooks installed");
}
}
