#include "Win10Ui.h"
#include "engine/Engine.h"
#include "settings/GraphicsSettings.h"
#include <android/log.h>
#include <atomic>
#include <dlfcn.h>
#include <memory>
#include <vector>

namespace features { namespace win10ui {
namespace {
bool available=false;
std::atomic<bool> refreshPending{false};
void* (*originalConstructor)(void*,void*,int,bool)=nullptr;
void* (*originalGlobals)(void*,const void*)=nullptr;
int (*originalUpdateScreens)(void*)=nullptr;
void* (*getChooser)(void*)=nullptr;
void* (*getLocalPlayer)(void*)=nullptr;
void (*setScreenType)(void*,int)=nullptr;
void (*popScreenNow)(void*)=nullptr;
void (*setStartMenu)(void*)=nullptr;
void (*pushHud)(void*)=nullptr;
void* gameplayVtable=nullptr;
void* (*jsonMember)(void*,const char*)=nullptr;
void (*jsonBool)(void*,bool)=nullptr;
void* (*jsonAssign)(void*,const void*)=nullptr;
void (*jsonDestroy)(void*)=nullptr;
using Screens=std::vector<std::shared_ptr<void>>;
static_assert(sizeof(std::shared_ptr<void>)==8 && sizeof(Screens)==12,"Screen stack ABI");

void* Constructor(void* chooser,void* client,int type,bool canInvite) {
    // UIScreenType: desktop=0, pocket=1, holographic=2.
    if(type!=2 && graphicssettings::Get().win10Ui) type=0;
    return originalConstructor(chooser,client,type,canInvite);
}
struct BooleanValue {
    // Json::Value in this engine uses a 16-byte representation and an 8-byte union.
    alignas(8) unsigned char storage[16];
    explicit BooleanValue(bool enabled) {jsonBool(storage,enabled);}
    ~BooleanValue() {jsonDestroy(storage);}
};
void SetBoolean(void* variables,const char* key,bool enabled) {
    BooleanValue value(enabled);
    jsonAssign(jsonMember(variables,key),value.storage);
}
void* Globals(void* variables,const void* chooser) {
    // Json::Value is returned via a hidden result pointer in r0; this is in r1.
    originalGlobals(variables,chooser);
    if(graphicssettings::Get().win10Ui && engine::At<int>(const_cast<void*>(chooser),8)!=2) {
        // Only select UI variants. Keep touch/mouse/gamepad and platform services
        // from the original global variables and Android implementation.
        SetBoolean(variables,"$win10_edition",true);
        SetBoolean(variables,"$pocket_edition",false);
        SetBoolean(variables,"$osx_edition",false);
        SetBoolean(variables,"$education_edition",false);
        SetBoolean(variables,"$not_education_edition",true);
        SetBoolean(variables,"$desktop_screen",true);
        SetBoolean(variables,"$pocket_screen",false);
    }
    return variables;
}
bool Refresh(void* client) {
    void* chooser=getChooser(client);
    if(!chooser || engine::At<int>(chooser,8)==2) return false;
    auto& screens=engine::At<Screens>(client,140);
    if(screens.empty() || screens.size()>32) return false;
    const bool playing=getLocalPlayer(client)!=nullptr;
    // Retain the gameplay screen and its world while replacing menus and HUD.
    if(playing && *static_cast<void**>(screens.front().get())!=static_cast<char*>(gameplayVtable)+8) {
        __android_log_print(ANDROID_LOG_WARN,"SunchWin10Ui","Deferring UI refresh for an alternative gameplay screen");
        return false;
    }
    setScreenType(chooser,graphicssettings::Get().win10Ui?0:1);
    const std::size_t keep=playing?1:0;
    while(screens.size()>keep) {
        const auto before=screens.size();
        popScreenNow(client);
        if(screens.size()>=before) return false;
    }
    if(playing) pushHud(chooser);
    else setStartMenu(chooser);
    __android_log_print(ANDROID_LOG_INFO,"SunchWin10Ui","UI switched to %s",graphicssettings::Get().win10Ui?"Windows 10":"Pocket");
    return true;
}
int UpdateScreens(void* client) {
    const int updated=originalUpdateScreens(client);
    // Native scheduled-screen processing runs after input callbacks return.
    // Never destroy the OptionsScreen inside its own toggle callback.
    if(engine::At<unsigned char>(client,404) || !refreshPending.exchange(false)) return updated;
    return Refresh(client)?1:updated;
}
const engine::Hook hooks[]={
    {"_ZN13ScreenChooserC2ER15MinecraftClient12UIScreenTypeb",0x37ce64,"2de9f04104462160",reinterpret_cast<void*>(Constructor),reinterpret_cast<void**>(&originalConstructor)},
    {"_ZNK13ScreenChooser16createGlobalVarsEv",0x385070,"2de9f047054686b0",reinterpret_cast<void*>(Globals),reinterpret_cast<void**>(&originalGlobals)},
    {"_ZN15MinecraftClient21updateScheduledScreenEv",0x330d04,"2de9f0470546d0f8",reinterpret_cast<void*>(UpdateScreens),reinterpret_cast<void**>(&originalUpdateScreens)},
};
}
void InstallVerified(void* library) {
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(library,name));ok=ok && target
    S("_ZNK15MinecraftClient16getScreenChooserEv",getChooser);
    S("_ZN15MinecraftClient14getLocalPlayerEv",getLocalPlayer);
    S("_ZN13ScreenChooser13setScreenTypeE12UIScreenType",setScreenType);
    S("_ZN15MinecraftClient10_popScreenEv",popScreenNow);
    S("_ZN13ScreenChooser18setStartMenuScreenEv",setStartMenu);
    S("_ZN13ScreenChooser13pushHudScreenEv",pushHud);
    S("_ZTV16InGamePlayScreen",gameplayVtable);
    S("_ZN4Json5ValueixEPKc",jsonMember);
    S("_ZN4Json5ValueC2Eb",jsonBool);
    S("_ZN4Json5ValueaSERKS0_",jsonAssign);
    S("_ZN4Json5ValueD2Ev",jsonDestroy);
#undef S
    if(!ok || !engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0]))) return;
    available=true;
    __android_log_print(ANDROID_LOG_INFO,"SunchWin10Ui","Native Windows 10 UI switch installed");
}
bool Available() {return available;}
void SetEnabled(bool enabled) {
    if(!available || graphicssettings::Get().win10Ui==enabled) return;
    graphicssettings::SetWin10Ui(enabled);
    refreshPending.store(true);
}
}}
