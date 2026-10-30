#include "ForceSprint.h"
#include "engine/Engine.h"
#include "settings/GraphicsSettings.h"
#include <android/log.h>
#include <cmath>
#include <dlfcn.h>

namespace features { namespace forcesprint {
namespace {
bool available=false;
int (*originalAiStep)(void*)=nullptr;
int (*originalSetSprinting)(void*,bool)=nullptr;
void* (*getInput)(void*)=nullptr;
void* (*getClient)(void*)=nullptr;
bool (*isInGame)(void*)=nullptr;
bool (*isAlive)(const void*)=nullptr;
bool (*isRiding)(const void*)=nullptr;
bool (*isFlying)(const void*)=nullptr;
bool (*isSneaking)(const void*)=nullptr;

// Only override cancellation inside the current LocalPlayer simulation step.
// Death, respawn, world teardown and other callers retain their normal setter.
thread_local void* activePlayer=nullptr;
thread_local bool active=false;
thread_local void* lastPlayer=nullptr;
thread_local bool applied=false;
struct StepScope {
    void* previousPlayer;
    bool previousActive;
    StepScope(void* player,bool enabled):previousPlayer(activePlayer),previousActive(active) {
        activePlayer=player;active=enabled;
    }
    ~StepScope() {activePlayer=previousPlayer;active=previousActive;}
};

bool ShouldForce(void* player) {
    if(!isAlive(player) || isRiding(player) || isFlying(player) || isSneaking(player)) return false;
    void* client=getClient(player);
    if(!client || !isInGame(client)) return false;
    void* input=getInput(player);
    if(!input) return false;
    // IDA: _updateXY stores strafe/forward at +4/+8. MoveInputHandler::tick
    // copies the sneak state to +60; +61 is sprint, despite the old SDK header.
    if(engine::At<unsigned char>(input,60)) return false;
    const float strafe=engine::At<float>(input,4),forward=engine::At<float>(input,8);
    // Positive forward input includes diagonally forward movement; strafing
    // alone, standing still and walking backward never force sprinting.
    return std::isfinite(strafe) && std::isfinite(forward) &&
        forward>.001f;
}
int SetSprinting(void* player,bool enabled) {
    if(active && activePlayer==player && ShouldForce(player)) enabled=true;
    return originalSetSprinting(player,enabled);
}
int AiStep(void* player) {
    if(lastPlayer!=player) {lastPlayer=player;applied=false;}
    const bool enabled=graphicssettings::Get().forceSprint;
    StepScope scope(player,enabled);
    if(!enabled) {
        if(applied) {originalSetSprinting(player,false);applied=false;}
        return originalAiStep(player);
    }
    // Apply before simulation for the current movement step, then re-evaluate
    // after the engine refreshes movement input. This also starts sprinting on
    // the first movement tick, without needing the vanilla double-tap gesture.
    if(ShouldForce(player)) {originalSetSprinting(player,true);applied=true;}
    const int result=originalAiStep(player);
    if(ShouldForce(player)) {originalSetSprinting(player,true);applied=true;}
    else if(applied) {originalSetSprinting(player,false);applied=false;}
    return result;
}
const engine::Hook hooks[]={
    {"_ZN11LocalPlayer6aiStepEv",0x42bc20,"d0f8ac1e2de9f047",reinterpret_cast<void*>(AiStep),reinterpret_cast<void**>(&originalAiStep)},
    {"_ZN11LocalPlayer12setSprintingEb",0x369860,"03682de9f0438db0",reinterpret_cast<void*>(SetSprinting),reinterpret_cast<void**>(&originalSetSprinting)},
};
}
void InstallVerified(void* library) {
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(library,name));ok=ok && target
    S("_ZN11LocalPlayer6_inputEv",getInput);
    S("_ZNK11LocalPlayer18getMinecraftClientEv",getClient);
    S("_ZN15MinecraftClient8isInGameEv",isInGame);
    S("_ZNK6Entity7isAliveEv",isAlive);
    S("_ZNK6Entity8isRidingEv",isRiding);
    S("_ZNK11LocalPlayer8isFlyingEv",isFlying);
    S("_ZNK3Mob10isSneakingEv",isSneaking);
#undef S
    if(!ok || !engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0]))) return;
    available=true;
    __android_log_print(ANDROID_LOG_INFO,"SunchSprint","Force sprint hooks installed");
}
bool Available() {return available;}
}}
