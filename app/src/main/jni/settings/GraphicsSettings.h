#pragma once
#include <string>
namespace graphicssettings {
struct Settings {bool showFps=false;bool vsync=true;bool blockInfo=false;bool dynamicLight=false;bool forceSprint=false;bool uiDebug=false;bool win10Ui=false;};
void Configure(const char* path);
Settings Get();
void SetShowFps(bool enabled);
void SetVsync(bool enabled);
void SetBlockInfo(bool enabled);
void SetDynamicLight(bool enabled);
void SetForceSprint(bool enabled);
void SetUiDebug(bool enabled);
void SetWin10Ui(bool enabled);
}
