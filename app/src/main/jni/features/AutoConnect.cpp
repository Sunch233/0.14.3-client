#include "AutoConnect.h"
#include "engine/Engine.h"
#include "settings/GraphicsSettings.h"
#include <android/log.h>
#include <cstdlib>
#include <dlfcn.h>
#include <functional>
#include <string>

namespace features { namespace autoconnect {
namespace {
// Signatures verified against libminecraftpe.so:
//   MinecraftClient::joinMultiplayer(const char*, int)            RVA 0x2e2710
//   ScreenChooser::pushProgressScreen(bool, string, fn, fn)       RVA 0x412780
//   ScreenChooser::setStartMenuScreen()                           RVA 0x3f7f50
using PushProgress=void(*)(void*,bool,const std::string&,std::function<void()>,std::function<void()>);

void (*joinMultiplayer)(void*,const char*,int)=nullptr;
void* (*getScreenChooser)(void*)=nullptr;
PushProgress pushProgressScreen=nullptr;
void (*originalStartMenu)(void*)=nullptr;

bool symbolsReady=false;
bool configured=false,attempted=false,mainMenuShown=false;
unsigned waited=0;
std::string address;
int port=19132;

bool ParseTarget() {
    const auto settings=graphicssettings::Get();
    if(settings.autoConnect.empty()) return false;
    const std::string& value=settings.autoConnect;
    const std::string::size_type colon=value.rfind(':');
    int parsedPort=19132;
    if(colon==std::string::npos) {
        address=value;
    } else {
        const std::string digits=value.substr(colon+1);
        if(digits.empty()) return false;
        for(std::string::size_type i=0;i<digits.size();++i) {
            const char c=digits[i];
            if(c<'0'||c>'9') return false;
        }
        parsedPort=std::atoi(digits.c_str());
        if(parsedPort<=0||parsedPort>65535) return false;
        address=value.substr(0,colon);
    }
    if(address.empty()) return false;
    port=parsedPort;
    return true;
}
void Join(void* client) {
    __android_log_print(ANDROID_LOG_INFO,"SunchAutoConnect","Connecting to %s:%d",address.c_str(),port);
    // Mirrors PlayScreenModel's external-server path: connect, then show the
    // progress screen so the UI does not stay on the start menu.
    joinMultiplayer(client,address.c_str(),port);
    void* chooser=getScreenChooser?getScreenChooser(client):nullptr;
    if(chooser && pushProgressScreen) {
        const std::string empty;
        pushProgressScreen(chooser,false,empty,std::function<void()>(),std::function<void()>());
    }
}
void StartMenu(void* self) {
    mainMenuShown=true;
    originalStartMenu(self);
}
// Separate batch: a mismatch only disables auto connect, never the HUD.
const engine::Hook hooks[]={
    {"_ZN13ScreenChooser18setStartMenuScreenEv",0x3f7f50,"2de9f041054696b0",
     reinterpret_cast<void*>(StartMenu),reinterpret_cast<void**>(&originalStartMenu),true},
};
}
void Initialize(void* library) {
    joinMultiplayer=reinterpret_cast<decltype(joinMultiplayer)>(
        dlsym(library,"_ZN15MinecraftClient15joinMultiplayerEPKci"));
    getScreenChooser=reinterpret_cast<decltype(getScreenChooser)>(
        dlsym(library,"_ZNK15MinecraftClient16getScreenChooserEv"));
    pushProgressScreen=reinterpret_cast<decltype(pushProgressScreen)>(
        dlsym(library,"_ZN13ScreenChooser18pushProgressScreenEbRKSsSt8functionIFvvEES4_"));
    symbolsReady=joinMultiplayer && getScreenChooser && pushProgressScreen;
    if(!symbolsReady)
        __android_log_print(ANDROID_LOG_WARN,"SunchAutoConnect","Auto connect helpers unavailable");
    if(!engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0])))
        __android_log_print(ANDROID_LOG_WARN,"SunchAutoConnect","Start menu hook unavailable");
}
void Tick(void* client) {
    if(attempted || !symbolsReady || !client) return;
    if(!configured) {
        // Configure() runs from Activity.onCreate, after JNI_OnLoad, so the
        // target is read here instead of during Initialize().
        if(++waited>300) {attempted=true;return;}
        configured=ParseTarget();
        if(!configured) return;
        __android_log_print(ANDROID_LOG_INFO,"SunchAutoConnect","Auto connect armed for %s:%d",address.c_str(),port);
    }
    if(!mainMenuShown) return;
    // setStartMenuScreen() fires near the end of MinecraftClient::init(), but
    // joinMultiplayer() throws std::future_error(no_state) while the engine's
    // load future at +372 is still unset. The mod is built without exceptions,
    // so calling early aborts the process. Wait for the engine to publish it.
    if(!engine::At<void*>(client,372)) return;
    attempted=true;
    Join(client);
}
bool Armed() {return symbolsReady && configured;}
}}
