#include "Features.h"
#include "AutoConnect.h"
#include "DynamicLight.h"
#include "ErrorReports.h"
#include "ForceSprint.h"
#include "Hud.h"
#include "MouseInput.h"
#include "TargetInfo.h"
#include "TitleBranding.h"
#include "UiDiagnostics.h"
#include "UiDebug.h"
#include "VSync.h"
#include "ViewDistance.h"
#include "Win10Ui.h"

namespace features {
void InstallVerified(void* lib) {
    errorreports::InstallVerified(lib);
    uidiagnostics::InstallVerified(lib);
    uidebug::InstallVerified(lib);
    win10ui::InstallVerified(lib);
    viewdistance::InstallVerified(lib);
    mouseinput::InstallVerified(lib);
    forcesprint::InstallVerified(lib);
    vsync::Initialize(lib);
    targetinfo::Initialize(lib);
    autoconnect::Initialize(lib);
    hud::InstallVerified(lib);
    dynamiclight::InstallVerified(lib);
    titlebranding::InstallVerified(lib);
}
}
