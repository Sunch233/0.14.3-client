#include "Features.h"
#include "DynamicLight.h"
#include "ErrorReports.h"
#include "ForceSprint.h"
#include "Hud.h"
#include "MouseInput.h"
#include "TargetInfo.h"
#include "TitleBranding.h"
#include "UiDiagnostics.h"
#include "VSync.h"

namespace features {
void InstallVerified(void* lib) {
    errorreports::InstallVerified(lib);
    uidiagnostics::InstallVerified(lib);
    mouseinput::InstallVerified(lib);
    forcesprint::InstallVerified(lib);
    vsync::Initialize(lib);
    targetinfo::Initialize(lib);
    hud::InstallVerified(lib);
    dynamiclight::InstallVerified(lib);
    titlebranding::InstallVerified(lib);
}
}
