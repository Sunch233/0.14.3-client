#include "FpsCounter.h"
#include "engine/Engine.h"
#include <chrono>

namespace features { namespace fpscounter {
namespace {
using engine::Number;
// Render-thread state. Count engine frames, not HUD passes or simulation ticks.
using FpsClock=std::chrono::steady_clock;
FpsClock::time_point fpsStart{},fpsPrevious{};
unsigned fpsFrames=0;
bool fpsEnabled=false,fpsSampling=false;
std::string fpsText="FPS: --";
}
void BeginFrame(bool enabled) {
    fpsEnabled=enabled;
    if(!fpsEnabled) {fpsSampling=false;return;}
    const auto now=FpsClock::now();
    if(!fpsSampling ||
       std::chrono::duration<double>(now-fpsPrevious).count()>1.0) {
        fpsSampling=true;fpsStart=now;fpsPrevious=now;fpsFrames=0;
        fpsText="FPS: --";return;
    }
    fpsPrevious=now;
    ++fpsFrames;
    const double elapsed=std::chrono::duration<double>(now-fpsStart).count();
    if(elapsed>=.5) {
        fpsText="FPS: "+Number(int(fpsFrames/elapsed+.5));
        fpsStart=now;fpsFrames=0;
    }
}
bool Enabled() {return fpsEnabled;}
const std::string& Text() {return fpsText;}
}}
