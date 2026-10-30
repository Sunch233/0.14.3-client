#pragma once
#include <string>
namespace graphicssettings {
struct Settings {bool showFps=false;bool vsync=true;bool blockInfo=false;bool dynamicLight=false;bool forceSprint=false;};
void Configure(const char* path);
Settings Get();
void SetShowFps(bool enabled);
void SetVsync(bool enabled);
void SetBlockInfo(bool enabled);
void SetDynamicLight(bool enabled);
void SetForceSprint(bool enabled);
}
