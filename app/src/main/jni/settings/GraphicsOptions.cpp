#include "GraphicsSettings.h"
#include "GraphicsOptions.h"
#include "features/DynamicLight.h"
#include "features/ForceSprint.h"
#include "features/UiDebug.h"
#include "features/TargetInfo.h"
#include "features/VSync.h"
#include "engine/Engine.h"
#include "InformationContent.h"
#include <android/log.h>
#include <dlfcn.h>
#include <memory>
#include <vector>

namespace settingsui {
using engine::At;
using engine::Color;
namespace {
// MCPE 0.14.3 ARMv7 / GNU STL. All object sizes and entrypoints verified in IDA.
struct Option {int type;std::string key;};
static_assert(sizeof(Option)==8 && sizeof(std::shared_ptr<void>)==8 && sizeof(std::vector<std::shared_ptr<void>>)==12,"Options ABI");
Option fpsOption{0,"帧数显示"};
Option vsyncOption{0,"垂直同步"};
Option dynamicLightOption{0,"动态光源"};
Option blockInfoOption{0,"方块/实体信息显示"};
Option forceSprintOption{0,"强制疾跑"};
Option uiDebugOption{0,"UI 调试"};
bool Custom(const Option* p) {return p==&fpsOption || p==&vsyncOption || p==&blockInfoOption || p==&dynamicLightOption || p==&forceSprintOption || p==&uiDebugOption;}
using PaneList=std::vector<std::shared_ptr<void>>;
void (*originalGenerate)(void*)=nullptr;
void (*originalButtons)(void*)=nullptr;
void (*originalScreenRender)(void*,int,int,float)=nullptr;
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
void (*labelConstructor)(void*,void*,const std::string&,const Color&,int,int,int,bool)=nullptr;
void (*labelText)(void*,const std::string&)=nullptr;
constexpr const char* settingsTitle="高级设置";
constexpr const char* moreTitle="更多功能";
constexpr const char* informationTitle="关于";
void* (*getFont)(void*)=nullptr;
void (*drawText)(void*,const std::string&,float,float,const Color&,bool)=nullptr;
int (*createButton)(void*,int,int,int,int,int,int)=nullptr;
void DeleteElement(void* p) {auto vt=*static_cast<void***>(p);reinterpret_cast<void(*)(void*)>(vt[1])(p);}
void AddLabel(void* group,void* client,const std::string& text) {
    void* raw=::operator new(0x58);
    const Color color{.8f,.8f,.8f,1.f};
    labelConstructor(raw,client,std::string(),color,0,0,0,true);
    labelText(raw,text);
    std::shared_ptr<void> label(raw,DeleteElement);
    auto vt=*static_cast<void***>(group);
    reinterpret_cast<void(*)(void*,std::shared_ptr<void>)>(vt[27])(group,label);
}
void* AddGroup(void* pane,const char* title) {
    // Original constructor translates titles; use the verified raw title layout.
    void* group=::operator new(0x48);
    containerConstructor(group,false,true,0,0,24,24);
    *static_cast<void**>(group)=static_cast<char*>(groupVtable)+8;
    new(static_cast<char*>(group)+68) std::string(title);
    At<PaneList>(pane,48).push_back(std::shared_ptr<void>(group,DeleteElement));
    return group;
}
std::shared_ptr<void> CreatePane() {
    void* raw=::operator new(0x70);
    paneConstructor(raw);
    return std::shared_ptr<void>(raw,DeleteElement);
}
void ScreenRender(void* screen,int x,int y,float dt) {
    originalScreenRender(screen,x,y,dt);
    if(At<int>(screen,228)==1) return;
    const auto& panes=At<PaneList>(screen,140);
    const auto& buttons=At<PaneList>(screen,200);
    if(panes.size()<2 || panes.size()!=buttons.size()) return;
    void* font=getFont(At<void*>(screen,20));
    if(!font) return;
    for(std::size_t i=panes.size()-2;i<panes.size();++i) {
        const auto& groups=At<PaneList>(panes[i].get(),48);
        if(groups.empty()) continue;
        const auto& title=At<std::string>(groups[0].get(),68);
        const char* mark=title==settingsTitle?"更多":title==informationTitle?"关于":nullptr;
        if(!mark) continue;
        void* button=buttons[i].get();
        const Color gold{1.f,.85f,.35f,1.f};
        drawText(font,mark,float(At<int>(button,8)+4),float(At<int>(button,12)+17),gold,false);
    }
}
void Buttons(void* screen) {
    originalButtons(screen);
    if(At<int>(screen,228)==1) return;
    auto& buttons=At<PaneList>(screen,200);
    // Append categories in the same order as their panes; vanilla lays them out.
    for(int i=0;i<2;++i)
        createButton(screen,int(buttons.size())+2,At<int>(screen,164),134,28,0,0);
}
void Generate(void* screen) {
    originalGenerate(screen);
    auto& panes=At<PaneList>(screen,140);
    const auto& buttons=At<PaneList>(screen,200);
    if(panes.size()<3 || panes.size()>5 || buttons.size()!=panes.size()+2) return;
    void* client=At<void*>(screen,20);
    auto settingsPane=CreatePane();
    void* settingsGroup=AddGroup(settingsPane.get(),settingsTitle);
    addOption(settingsGroup,fpsOption,client);
    addOption(settingsGroup,vsyncOption,client);
    void* moreGroup=AddGroup(settingsPane.get(),moreTitle);
    addOption(moreGroup,blockInfoOption,client);
    addOption(moreGroup,dynamicLightOption,client);
    addOption(moreGroup,forceSprintOption,client);
    addOption(moreGroup,uiDebugOption,client);
    panes.push_back(std::move(settingsPane));

    auto informationPane=CreatePane();
    void* history=AddGroup(informationPane.get(),informationTitle);
    for(const char* line:informationcontent::updates) AddLabel(history,client,line);
    void* credits=AddGroup(informationPane.get(),"更新记录");
    for(const char* line:informationcontent::credits) AddLabel(credits,client,line);
    panes.push_back(std::move(informationPane));
}
bool Boolean(void* self,const Option* p) {
    if(p==&fpsOption) return graphicssettings::Get().showFps;
    if(p==&vsyncOption) return graphicssettings::Get().vsync;
    if(p==&blockInfoOption) return graphicssettings::Get().blockInfo;
    if(p==&dynamicLightOption) return graphicssettings::Get().dynamicLight;
    if(p==&forceSprintOption) return graphicssettings::Get().forceSprint;
    if(p==&uiDebugOption) return graphicssettings::Get().uiDebug;
    return originalBoolean(self,p);
}
bool CanModify(void* self,const Option* p) {
    if(p==&uiDebugOption) return features::uidebug::Available();
    if(p==&forceSprintOption) return features::forcesprint::Available();
    if(p==&dynamicLightOption) return features::dynamiclight::Available();
    if(p==&vsyncOption) return features::vsync::Available();
    if(p==&blockInfoOption) return features::targetinfo::Available();
    return Custom(p)?true:originalCanModify(self,p);
}
void Toggle(void* self,const Option* p,int direction) {
    if(p==&fpsOption) {graphicssettings::SetShowFps(!graphicssettings::Get().showFps);return;}
    if(p==&vsyncOption) {graphicssettings::SetVsync(!graphicssettings::Get().vsync);return;}
    if(p==&blockInfoOption) {graphicssettings::SetBlockInfo(!graphicssettings::Get().blockInfo);return;}
    if(p==&dynamicLightOption) {graphicssettings::SetDynamicLight(!graphicssettings::Get().dynamicLight);return;}
    if(p==&forceSprintOption) {graphicssettings::SetForceSprint(!graphicssettings::Get().forceSprint);return;}
    if(p==&uiDebugOption) {graphicssettings::SetUiDebug(!graphicssettings::Get().uiDebug);return;}
    originalToggle(self,p,direction);
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
#define O(name,rva,prefix,fn,original) {name,rva,prefix,reinterpret_cast<void*>(fn),reinterpret_cast<void**>(&original)}
const engine::Hook hooks[]={
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
void InstallVerified(void* lib) {
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(lib,name));ok=ok && target
    S("_ZN11OptionsPaneC2Ev",paneConstructor);
    S("_ZN19GuiElementContainerC2Ebbiiii",containerConstructor);
    S("_ZTV12OptionsGroup",groupVtable);
    S("_ZN19GuiElementContainer6renderEP15MinecraftClientii",containerRender);
    S("_ZN10GuiElement10isSelectedEv",isSelected);
    S("_ZN11OptionsItem9_isLockedEv",isLocked);
    S("_ZN12OptionsGroup13addOptionItemERKN7Options6OptionER15MinecraftClient",addOption);
    S("_ZN5LabelC2ER15MinecraftClientRKSsRK5Coloriiib",labelConstructor);
    S("_ZN5Label10setTextRawERKSs",labelText);
    S("_ZNK15MinecraftClient7getFontEv",getFont);
    S("_ZN4Font10drawShadowERKSsffRK5Colorb",drawText);
    S("_ZN13OptionsScreen20createCategoryButtonEiiiiii",createButton);
#undef S
    if(!ok) return;
    if(engine::InstallHooks(lib,hooks,sizeof(hooks)/sizeof(hooks[0])))
        __android_log_print(ANDROID_LOG_INFO,"ClientSettings","Native advanced settings UI hooks installed");
}
}
