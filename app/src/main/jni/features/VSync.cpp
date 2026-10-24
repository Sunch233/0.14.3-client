#include "VSync.h"
#include <android/log.h>
#include <cstdint>
#include <dlfcn.h>

namespace features { namespace vsync {
namespace {
unsigned char** rendererSettings=nullptr;
}
void Initialize(void* lib) {
    auto* singleton=dlsym(lib,"_ZN3mce9SingletonINS_16RendererSettingsESt10unique_ptrIS1_St14default_deleteIS1_EEE8instanceE");
    Dl_info image{};
    if(singleton && dladdr(singleton,&image) &&
       reinterpret_cast<uintptr_t>(singleton)-reinterpret_cast<uintptr_t>(image.dli_fbase)==0x75db9c)
        rendererSettings=static_cast<unsigned char**>(singleton);
    else
        __android_log_print(ANDROID_LOG_WARN,"ClientSettings","VSync unavailable: renderer singleton mismatch");
}
bool Available() {return rendererSettings && *rendererSettings;}
void Apply(bool enabled) {
    if(Available()) (*rendererSettings)[48]=enabled?1:0;
}
}}
