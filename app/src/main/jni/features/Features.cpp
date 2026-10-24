#include "Features.h"
#include "DynamicLight.h"
#include "Hud.h"
#include "TargetInfo.h"
#include "TitleBranding.h"
#include "VSync.h"

namespace features {
void InstallVerified(void* lib) {
    vsync::Initialize(lib);
    targetinfo::Initialize(lib);
    hud::InstallVerified(lib);
    dynamiclight::InstallVerified(lib);
    titlebranding::InstallVerified(lib);
}
}
