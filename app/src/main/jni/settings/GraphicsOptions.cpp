#include "GraphicsSettings.h"
#include "DynamicLight.h"
#include "TitleBranding.h"
#include "InformationContent.h"
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
Option vsyncOption{0,"垂直同步"};
Option dynamicLightOption{0,"动态光源"};
Option blockInfoOption{0,"方块/实体信息显示"};
unsigned char** rendererSettings=nullptr;
void ApplyVsync() {
    if(rendererSettings && *rendererSettings)
        (*rendererSettings)[48]=Get().vsync?1:0;
}
std::string Number(int value) {char buffer[32];snprintf(buffer,sizeof(buffer),"%d",value);return buffer;}
bool Custom(const Option* p) {return p==&fpsOption || p==&vsyncOption || p==&blockInfoOption || p==&dynamicLightOption;}
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
void (*labelConstructor)(void*,void*,const std::string&,const Color&,int,int,int,bool)=nullptr;
void (*labelText)(void*,const std::string&)=nullptr;
constexpr const char* settingsTitle="高级设置";
constexpr const char* moreTitle="更多功能";
constexpr const char* informationTitle="关于";
void* (*getFont)(void*)=nullptr;
void (*drawText)(void*,const std::string&,float,float,const Color&,bool)=nullptr;
int (*createButton)(void*,int,int,int,int,int,int)=nullptr;
void (*blockAndData)(void*,void*,int,int,int)=nullptr; // out{u8 id,u8 data},BlockSource,x,y,z
void (*itemGetName)(std::string*,void*)=nullptr; // ItemInstance::getName(out,instance)
int (*lineLength)(void*,const std::string&,float,bool)=nullptr;
int (*guiScreenWidth)(void*)=nullptr;
void* (*getInput)(void*)=nullptr;
bool (*useFreeformPick)(void*)=nullptr;
bool (*allowPicking)(void*)=nullptr;
void** blockArray=nullptr; // Block::mBlocks
bool (*isMob)(const void*)=nullptr;
int (*mobHealth)(const void*)=nullptr;
int (*mobMaxHealth)(const void*)=nullptr;
const std::string& (*entityNameTag)(const void*)=nullptr;
void (*mobNameKey)(std::string*,int)=nullptr;
void (*translate)(std::string*,const std::string&)=nullptr;
void (*loadTexture)(void*,void*,const std::string&,int)=nullptr;
void (*destroyTexture)(void*)=nullptr;
void* (*screenRendererInstance)()=nullptr;
void (*blitTexture)(void*,const void*,int,int,int,int,int,int,int,int,void*)=nullptr;
bool entityTooltipAvailable=false;
void* (*getTextures)(void*)=nullptr;
void* (*getItemRenderer)()=nullptr;
// Same entrypoint and argument order as InventoryScreen::_renderGridItem.
// icon frame, x, y, alpha, brightness, scale, force glint.
void (*renderGuiItem)(void*,const void*,int,float,float,float,float,float,bool)=nullptr;
void (*patchFactoryConstructor)(void*,void*,const std::string&)=nullptr;
void (*createPatch)(void**,void*,const int*,int,int,float,float)=nullptr;
void (*patchSetSize)(void*,float,float)=nullptr;
void (*patchSetExcluded)(void*,int)=nullptr;
void (*fillRect)(void*,float,float,float,float,const Color&)=nullptr;
void (*setShaderColor)(void*,const Color&)=nullptr;
void* shaderColor=nullptr;
void (*patchDraw)(void*,void*,float,float)=nullptr;
void* tessellator=nullptr;
void* tooltipPatch=nullptr;
void* tooltipTextures=nullptr;

// NinePatchFactory's native ABI: TextureGroup*, GNU string, width, height.
struct PatchFactory {
    alignas(4) unsigned char storage[16];
    explicit PatchFactory(void* textures) {
        patchFactoryConstructor(storage,textures,std::string("gui/purpleBorder.png"));
    }
    ~PatchFactory() {
        reinterpret_cast<std::string*>(storage+4)->~basic_string();
    }
};
// asItemInstance returns an owning ItemInstance; its CompoundTag is at +8.
struct BlockItem {
    alignas(8) unsigned char storage[40]{};
    BlockItem(void* block,void* region,const int* pos,int data) {
        void* const* vt=*static_cast<void* const**>(block);
        reinterpret_cast<void(*)(void*,void*,void*,const int*,int)>(vt[50])
            (storage,block,region,pos,data);
    }
    ~BlockItem() {
        void* tag=At<void*>(storage,8);
        if(tag) {
            auto vt=*static_cast<void***>(tag);
            reinterpret_cast<void(*)(void*)>(vt[1])(tag);
        }
    }
};
void DrawTooltipBackground(void* textures,float x,float y,float width,float height) {
    if(tooltipTextures!=textures) {
        if(tooltipPatch) {
            auto vt=*static_cast<void***>(tooltipPatch);
            reinterpret_cast<void(*)(void*)>(vt[1])(tooltipPatch);
            tooltipPatch=nullptr;
        }
        tooltipTextures=textures;
    }
    if(!tooltipPatch) {
        PatchFactory factory(textures);
        const int rectangle[4]={0,0,16,16};
        // Match vanilla HoverTextRenderer: 3-pixel corners from purpleBorder.png.
        createPatch(&tooltipPatch,factory.storage,rectangle,3,3,width,height);
        // Only replace the center quad; preserve all eight purple border patches.
        if(tooltipPatch) patchSetExcluded(tooltipPatch,1<<4);
    }
    if(tooltipPatch) {
        const Color previousColor=At<Color>(shaderColor,0);
        const Color background{16.f/255.f,0.f,16.f/255.f,.55f};
        // ScreenRenderer::fill does not access its instance (verified in IDA).
        alignas(4) unsigned char screenRenderer[4]{};
        fillRect(screenRenderer,x+3.f,y+3.f,x+width-3.f,y+height-3.f,background);
        setShaderColor(shaderColor,previousColor);
        patchSetSize(tooltipPatch,width,height);
        patchDraw(tooltipPatch,tessellator,x,y);
    }
}
std::string FitTooltipText(void* font,std::string text,int maxWidth) {
    if(lineLength(font,text,1.f,false)<=maxWidth) return text;
    const std::string suffix="...";
    while(!text.empty() && lineLength(font,text+suffix,1.f,false)>maxWidth) {
        // Remove a complete UTF-8 codepoint, including translated block names.
        std::size_t start=text.size()-1;
        while(start && (static_cast<unsigned char>(text[start])&0xc0)==0x80) --start;
        text.erase(start);
    }
    return text+suffix;
}

// Render-thread state. Count engine frames, not HUD passes or simulation ticks.
using FpsClock=std::chrono::steady_clock;
FpsClock::time_point fpsStart{},fpsPrevious{};
unsigned fpsFrames=0;
bool fpsEnabled=false,fpsSampling=false;
std::string fpsText="FPS: --";
std::string blockText;
bool blockInfoEnabled=false;
// Meta-aware localized name through the inventory tooltip chain:
// asItemInstance (vtable+200) builds the block's ItemInstance with the
// world data value, then ItemInstance::getName resolves the name. Some
// block subclasses have stub name overrides (leaves, skulls), so the
// item-side overrides (LeafBlockItem, SkullItem, ClothBlockItem, ...)
// supply the proper per-meta keys.
std::string BlockName(void* block,void* instance) {
    void* const* vt=*static_cast<void* const**>(block);
    std::string name;
    if(At<void*>(instance,12))
        // ItemInstance::getName: the exact inventory tooltip chain. Per-meta
        // item overrides handle what the block stubs do not (leaf types via
        // tile.leaves.*, skull types via item.skull.*, wool colors, ...).
        itemGetName(&name,instance);
    else
        // Blocks without an item (fire, bedrock, ...): ask the block itself.
        reinterpret_cast<void(*)(std::string*,void*,void*)>(vt[81])(&name,block,instance);
    // Untranslated keys look like "tile.xxx[.yyy].name"; strip to the raw ids.
    if(name.size()>5 && name.compare(name.size()-5,5,".name")==0) name.erase(name.size()-5);
    if(name.size()>5 && name.compare(0,5,"tile.")==0) name.erase(0,5);
    return name;
}
// Per-frame entity access only. Never retain a hit pointer across engine frames.
void DrawEntityInfo(void* client,void* font,void* entity) {
    if(!entity || !entityTooltipAvailable) return;
    auto vt=*static_cast<void***>(entity);
    const int type=reinterpret_cast<int(*)(const void*)>(vt[83])(entity);
    const bool living=isMob(entity);
    std::string name=entityNameTag(entity);
    if(name.empty()) {
        std::string key;
        mobNameKey(&key,type);
        key+=".name";
        translate(&name,key);
        if(name.empty() || name==key) name="实体 #"+Number(type&255);
    }
    int health=0,maximum=0;
    if(living) {
        maximum=std::max(0,mobMaxHealth(entity));
        health=std::max(0,std::min(maximum,mobHealth(entity)));
    }
    void* gui=At<void*>(client,304);
    void* textures=getTextures(client);
    if(!gui || !textures) return;
    const int screenWidth=guiScreenWidth(gui);
    if(screenWidth<96) return;
    const int padding=6;
    name=FitTooltipText(font,name,screenWidth-20);
    const int hearts=std::min(20,maximum/2+maximum%2);
    const int rows=(hearts+9)/10;
    const std::string numeric=maximum>40?Number(health)+" / "+Number(maximum):"";
    const int heartWidth=std::min(hearts,10)*8+1;
    int width=std::max(80,lineLength(font,name,1.f,false)+padding*2);
    width=std::max(width,lineLength(font,numeric,1.f,false)+padding*2);
    width=std::max(width,heartWidth+padding*2);
    const int height=padding*2+8+(rows?rows*10+2:0)+(numeric.empty()?0:10);
    const int left=(screenWidth-width)/2,top=4;
    DrawTooltipBackground(textures,float(left),float(top),float(width),float(height));
    drawText(font,name,float(left+padding),float(top+padding),Color{1.f,1.f,1.f,1.f},false);
    int y=top+padding+10;
    if(hearts) {
        alignas(4) unsigned char icons[16]{};
        loadTexture(icons,textures,std::string("gui/icons.png"),0);
        void* renderer=screenRendererInstance();
        for(int i=0;i<hearts;++i) {
            const int x=left+padding+(i%10)*8;
            const int rowY=y+(i/10)*10;
            // Same atlas cells as the original HUD: empty, full and half heart.
            blitTexture(renderer,icons,x,rowY,16,0,9,9,0,0,nullptr);
            const int remaining=health-i*2;
            if(remaining>0) blitTexture(renderer,icons,x,rowY,remaining>=2?52:61,0,9,9,0,0,nullptr);
        }
        destroyTexture(icons);
        y+=rows*10;
    }
    if(!numeric.empty()) {
        drawText(font,numeric,float(left+padding),float(y),Color{1.f,.7f,.7f,1.f},false);
        y+=10;
    }

}
void DrawBlockInfo(void* client,void* font) {
    void* input=getInput(client);
    if(!input) return;
    // Touch picking leaves the last HitResult intact after finger-up. Match the
    // native interaction state instead of treating that cached hit as active.
    // Split controls / mouse / gamepad keep their continuous crosshair picking.
    if(useFreeformPick(input) && !allowPicking(input)) return;
    void* gameRenderer=At<void*>(client,104);
    void* level=gameRenderer?At<void*>(gameRenderer,8):nullptr;
    if(!level) return;
    // Level::mHitResult, refreshed each frame by InGamePlayScreen::_pick for
    // the active pick (touch ray or crosshair), so both input modes are covered.
    char* hit=static_cast<char*>(level)+344;
    if(At<int>(hit,0)==1) {
        DrawEntityInfo(client,font,At<void*>(hit,32));
        return;
    }
    if(At<int>(hit,0)!=0) return; // 0 = block
    const int x=At<int>(hit,8),y=At<int>(hit,12),z=At<int>(hit,16);
    void* player=At<void*>(client,288);
    if(!player) return;
    void* region=At<void*>(player,364);
    if(!region) return;
    struct Tile {unsigned char id,data;};
    Tile tile{};
    blockAndData(&tile,region,x,y,z);
    if(!tile.id) return;
    const int pos[3]={x,y,z};
    void* block=blockArray?blockArray[tile.id]:nullptr;
    if(!block) return;
    BlockItem item(block,region,pos,tile.data);
    // Resolve translated names on every draw so language/world changes are reflected.
    blockText=BlockName(block,item.storage);
    if(blockText.empty()) blockText=Number(tile.id);
    const std::string meta=Number(tile.id)+":"+Number(tile.data);
    void* gui=At<void*>(client,304);
    void* textures=getTextures(client);
    void* renderer=getItemRenderer();
    if(!gui || !textures || !renderer) return;
    const int screenWidth=guiScreenWidth(gui);
    if(screenWidth<64) return;
    // Logical GUI pixels scale with the game's UI. Reserve 16px for the icon.
    const int padding=6,textOffset=28,height=28;
    blockText=FitTooltipText(font,blockText,screenWidth-8-textOffset-padding);
    const int contentWidth=std::max(lineLength(font,blockText,1.f,false),lineLength(font,meta,1.f,false));
    const int width=std::max(80,textOffset+contentWidth+padding);
    const int left=(screenWidth-width)/2,top=4;
    DrawTooltipBackground(textures,float(left),float(top),float(width),float(height));
    // Use the inventory's 16x16 slot origin, not renderGuiItemCorrect's legacy
    // matrix origin. The modern path resolves mItemGraphics and preserves the
    // texture UV/alpha/tint for plants and other non-cube block icons.
    constexpr int iconSize=16;
    const float iconX=float(left+padding);
    const float iconY=float(top)+(float(height)-iconSize)*.5f;
    renderGuiItem(renderer,item.storage,0,iconX,iconY,1.f,1.f,1.f,false);
    drawText(font,blockText,float(left+textOffset),float(top+6),Color{1.f,1.f,1.f,1.f},false);
    drawText(font,meta,float(left+textOffset),float(top+16),Color{.33f,.33f,1.f,1.f},false);
}
void HudRender(void* self,void* client,void* control,int pass) {
    originalHudRender(self,client,control,pass);
    void* font=getFont(client);
    if(!font) return;
    if(fpsEnabled) drawText(font,fpsText,4.f,4.f,Color{1.f,1.f,1.f,1.f},false);
    if(blockInfoEnabled) DrawBlockInfo(client,font);
}

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
    panes.push_back(std::move(settingsPane));

    auto informationPane=CreatePane();
    void* history=AddGroup(informationPane.get(),informationTitle);
    for(const char* line:informationcontent::updates) AddLabel(history,client,line);
    void* credits=AddGroup(informationPane.get(),"更新记录");
    for(const char* line:informationcontent::credits) AddLabel(credits,client,line);
    panes.push_back(std::move(informationPane));
}
bool Boolean(void* self,const Option* p) {
    if(p==&fpsOption) return Get().showFps;
    if(p==&vsyncOption) return Get().vsync;
    if(p==&blockInfoOption) return Get().blockInfo;
    if(p==&dynamicLightOption) return Get().dynamicLight;
    return originalBoolean(self,p);
}
bool CanModify(void* self,const Option* p) {
    if(p==&dynamicLightOption) return dynamiclight::Available();
    if(p==&vsyncOption) return rendererSettings && *rendererSettings;
    return Custom(p)?true:originalCanModify(self,p);
}
void Toggle(void* self,const Option* p,int direction) {
    if(p==&fpsOption) {SetShowFps(!Get().showFps);return;}
    if(p==&vsyncOption) {SetVsync(!Get().vsync);return;}
    if(p==&blockInfoOption) {SetBlockInfo(!Get().blockInfo);return;}
    if(p==&dynamicLightOption) {SetDynamicLight(!Get().dynamicLight);return;}
    originalToggle(self,p,direction);
}
void (*originalFrame)(void*,float)=nullptr;
void Frame(void* self,float dt) {
    ApplyVsync();
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
    blockInfoEnabled=Get().blockInfo;
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
    auto* singleton=dlsym(lib,"_ZN3mce9SingletonINS_16RendererSettingsESt10unique_ptrIS1_St14default_deleteIS1_EEE8instanceE");
    if(singleton && reinterpret_cast<uintptr_t>(singleton)-reinterpret_cast<uintptr_t>(image.dli_fbase)==0x75db9c)
        rendererSettings=static_cast<unsigned char**>(singleton);
    else
        __android_log_print(ANDROID_LOG_WARN,"ClientSettings","VSync unavailable: renderer singleton mismatch");
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
    S("_ZN11BlockSource15getBlockAndDataEiii",blockAndData);
    S("_ZNK12ItemInstance7getNameEv",itemGetName);
    S("_ZNK4Font13getLineLengthERKSsfb",lineLength);
    S("_ZNK7GuiData14getScreenWidthEv",guiScreenWidth);
    S("_ZNK15MinecraftClient8getInputEv",getInput);
    S("_ZNK21MinecraftInputHandler24useFreeformPickDirectionEv",useFreeformPick);
    S("_ZNK21MinecraftInputHandler12allowPickingEv",allowPicking);
    S("_ZN5Block7mBlocksE",blockArray);
    S("_ZNK15MinecraftClient11getTexturesEv",getTextures);
    S("_ZN12ItemRenderer11getInstanceEv",getItemRenderer);
    S("_ZN12ItemRenderer16renderGuiItemNewEPK12ItemInstanceifffffb",renderGuiItem);
    S("_ZN16NinePatchFactoryC2EPN3mce12TextureGroupERKSs",patchFactoryConstructor);
    S("_ZN16NinePatchFactory23createUniqueSymmetricalERK12IntRectangleiiff",createPatch);
    S("_ZN14NinePatchLayer7setSizeEff",patchSetSize);
    S("_ZN14NinePatchLayer11setExcludedEi",patchSetExcluded);
    S("_ZNK14ScreenRenderer4fillEffffRK5Color",fillRect);
    S("_ZN11ShaderColor8setColorERK5Color",setShaderColor);
    S("currentShaderColor",shaderColor);
    S("_ZN14NinePatchLayer4drawER11Tessellatorff",patchDraw);
    S("_ZN11Tessellator8instanceE",tessellator);
#undef S
    // A missing entity helper must not disable the existing settings/FPS hooks.
    bool entityOk=true;
#define E(name,target) target=reinterpret_cast<decltype(target)>(dlsym(lib,name));entityOk=entityOk && target
    E("_ZN15EntityClassTree5isMobERK6Entity",isMob);
    E("_ZNK3Mob9getHealthEv",mobHealth);
    E("_ZNK3Mob12getMaxHealthEv",mobMaxHealth);
    E("_ZNK6Entity10getNameTagEv",entityNameTag);
    E("_ZN10MobFactory12GetMobNameIDE10EntityType",mobNameKey);
    E("_ZN4I18n3getERKSs",translate);
    E("_ZN3mce12TextureGroup10getTextureERKSs15TextureLocation",loadTexture);
    E("_ZN3mce10TexturePtrD2Ev",destroyTexture);
    E("_ZN14ScreenRenderer9singletonEv",screenRendererInstance);
    E("_ZN14ScreenRenderer4blitERKN3mce10TexturePtrEiiiiiiiiPNS0_11MaterialPtrE",blitTexture);
#undef E
    entityTooltipAvailable=entityOk;
    if(!entityOk) __android_log_print(ANDROID_LOG_WARN,"ClientSettings","Entity tooltip helpers unavailable");
    if(!ok) return;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)
        MSHookFunction(targets[i],hooks[i].replace,hooks[i].original);
    dynamiclight::InstallVerified(lib);
    titlebranding::InstallVerified(lib);
    __android_log_print(ANDROID_LOG_INFO,"ClientSettings","Native advanced graphics hooks installed");
}
}
