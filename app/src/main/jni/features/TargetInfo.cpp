#include "TargetInfo.h"
#include "engine/Engine.h"
#include <android/log.h>
#include <algorithm>
#include <dlfcn.h>
#include <string>

namespace features { namespace targetinfo {
namespace {
using engine::At;
using engine::Color;
using engine::Number;
bool available=false;
void (*drawText)(void*,const std::string&,float,float,const Color&,bool)=nullptr;
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

std::string blockText;
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
}
void Initialize(void* lib) {
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(lib,name));ok=ok && target
    S("_ZN4Font10drawShadowERKSsffRK5Colorb",drawText);
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
    // Entity helpers are optional; block inspection remains available without them.
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
    available=ok;
    if(!available) __android_log_print(ANDROID_LOG_WARN,"SunchTargetInfo","Block tooltip helpers unavailable");
}
bool Available() {return available;}
void Render(void* client,void* font) {
    if(available) DrawBlockInfo(client,font);
}
}}
