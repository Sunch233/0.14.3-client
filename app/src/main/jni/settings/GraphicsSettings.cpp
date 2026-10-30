#include "GraphicsSettings.h"
#include <cstdio>
#include <mutex>
#include <android/log.h>
namespace graphicssettings {
namespace {
std::mutex mutex;
Settings settings;
std::string filePath;
bool Save() {
    if(filePath.empty()) return false;
    const std::string temp=filePath+".tmp";
    FILE* f=std::fopen(temp.c_str(),"wb");
    if(!f) return false;
    bool ok=std::fprintf(f,"version=1\nshowFps=%d\nvsync=%d\nblockInfo=%d\ndynamicLight=%d\nforceSprint=%d\n",
        settings.showFps?1:0,settings.vsync?1:0,settings.blockInfo?1:0,settings.dynamicLight?1:0,settings.forceSprint?1:0)>0;
    if(std::fclose(f)!=0) ok=false;
    if(ok) ok=std::rename(temp.c_str(),filePath.c_str())==0;
    if(!ok) std::remove(temp.c_str());
    return ok;
}
}
void Configure(const char* path) {
    std::lock_guard<std::mutex> lock(mutex);
    filePath=path?path:"";
    settings=Settings{};
    FILE* f=std::fopen(filePath.c_str(),"rb");
    if(!f) return;
    char line[160],tail;
    int version=0;
    Settings loaded;
    while(std::fgets(line,sizeof(line),f)) {
        int value;
        if(std::sscanf(line,"version=%d %c",&value,&tail)==1) version=value;
        else if(std::sscanf(line,"showFps=%d %c",&value,&tail)==1 && (value==0 || value==1)) loaded.showFps=value!=0;
        else if(std::sscanf(line,"vsync=%d %c",&value,&tail)==1 && (value==0 || value==1)) loaded.vsync=value!=0;
        else if(std::sscanf(line,"blockInfo=%d %c",&value,&tail)==1 && (value==0 || value==1)) loaded.blockInfo=value!=0;
        else if(std::sscanf(line,"dynamicLight=%d %c",&value,&tail)==1 && (value==0 || value==1)) loaded.dynamicLight=value!=0;
        else if(std::sscanf(line,"forceSprint=%d %c",&value,&tail)==1 && (value==0 || value==1)) loaded.forceSprint=value!=0;
    }
    std::fclose(f);
    if(version==1) settings=loaded;
}
Settings Get() {std::lock_guard<std::mutex> lock(mutex);return settings;}
void SetShowFps(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    if(settings.showFps==enabled) return;
    settings.showFps=enabled;
    if(!Save()) __android_log_print(ANDROID_LOG_WARN,"ClientSettings","Could not save FPS setting");
}
void SetVsync(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    if(settings.vsync==enabled) return;
    settings.vsync=enabled;
    if(!Save()) __android_log_print(ANDROID_LOG_WARN,"ClientSettings","Could not save VSync setting");
}
void SetBlockInfo(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    if(settings.blockInfo==enabled) return;
    settings.blockInfo=enabled;
    if(!Save()) __android_log_print(ANDROID_LOG_WARN,"ClientSettings","Could not save block info setting");
}

void SetDynamicLight(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    if(settings.dynamicLight==enabled) return;
    settings.dynamicLight=enabled;
    if(!Save()) __android_log_print(ANDROID_LOG_WARN,"ClientSettings","Could not save dynamic light setting");
}

void SetForceSprint(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    if(settings.forceSprint==enabled) return;
    settings.forceSprint=enabled;
    if(!Save()) __android_log_print(ANDROID_LOG_WARN,"ClientSettings","Could not save force sprint setting");
}

}
