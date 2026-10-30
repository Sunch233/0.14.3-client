#include "Features.h"
#include "DynamicLight.h"
#include "Hud.h"
#include "MouseInput.h"
#include "TargetInfo.h"
#include "TitleBranding.h"
#include "VSync.h"

namespace features {
void InstallVerified(void* lib) {
    mouseinput::InstallVerified(lib);
    vsync::Initialize(lib);
    targetinfo::Initialize(lib);
    hud::InstallVerified(lib);
    dynamiclight::InstallVerified(lib);
    titlebranding::InstallVerified(lib);
}
}
