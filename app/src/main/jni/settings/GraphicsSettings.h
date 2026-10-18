#pragma once
#include <string>
namespace graphicssettings {
struct Settings {bool showFps=false;bool vsync=true;bool blockInfo=false;bool dynamicLight=false;};
void Configure(const char* path);
Settings Get();
void SetShowFps(bool enabled);
void SetVsync(bool enabled);
void SetBlockInfo(bool enabled);
void SetDynamicLight(bool enabled);
void BeginHudFrame();
void InstallVerified(void* library);
bool Sha256File(const char* path,std::string& digest);
}
