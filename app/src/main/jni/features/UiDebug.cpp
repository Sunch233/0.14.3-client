#include "UiDebug.h"
#include "engine/Engine.h"
#include "settings/GraphicsSettings.h"
#include <android/log.h>
#include <dlfcn.h>

namespace features { namespace uidebug {
namespace {
bool available=false;
void (*setDebugRendering)(bool)=nullptr;
void (*originalRender)(void*,void*)=nullptr;

void Render(void* view,void* context) {
    // This engine method is static: the only argument is the enabled flag.
    // Apply on the render thread, including menus and screens without a world.
    setDebugRendering(graphicssettings::Get().uiDebug);
    originalRender(view,context);
}
const engine::Hook hooks[]={
    {"_ZN10ScreenView14setupAndRenderER15UIScreenContext",0x41aa14,"2de9f04f2ded028b",reinterpret_cast<void*>(Render),reinterpret_cast<void**>(&originalRender)},
};
}
void InstallVerified(void* library) {
    // ScreenView::setDebugRendering(bool) writes sRenderDebugControls, which
    // _renderBatches checks before calling the engine's _debugRenderControl.
    setDebugRendering=reinterpret_cast<decltype(setDebugRendering)>(dlsym(library,"_ZN10ScreenView17setDebugRenderingEb"));
    if(!setDebugRendering || !engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0]))) return;
    available=true;
    __android_log_print(ANDROID_LOG_INFO,"SunchUiDebug","Native UI control debugging installed");
}
bool Available() {return available;}
}}
