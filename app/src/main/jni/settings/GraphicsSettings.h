#pragma once
#include <string>
namespace graphicssettings {
struct Settings {bool showFps=false;};
void Configure(const char* path);
Settings Get();
void SetShowFps(bool enabled);
void BeginHudFrame();
void InstallVerified(void* library);
bool Sha256File(const char* path,std::string& digest);
}
