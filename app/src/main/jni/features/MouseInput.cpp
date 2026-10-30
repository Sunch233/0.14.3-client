#include "MouseInput.h"
#include "MouseState.h"
#include "engine/Engine.h"
#include <android/input.h>
#include <android/log.h>
#include <atomic>
#include <dlfcn.h>
#include <jni.h>
#include <vector>

namespace features { namespace mouseinput {
namespace {
// All engine calls and mutable conversion state belong to the native game
// thread. Java only publishes window/device state through atomics.
std::atomic<bool> available{false},focused{false},connected{false},captureWanted{false},captureReleased{false};
std::atomic<unsigned> resetGeneration{0};
unsigned appliedReset=0;
MouseState mouse;
WheelState wheel;
void* client=nullptr;
void* lastScreen=nullptr;
int width=32768,height=32768;
bool gameplay=false;
int (*originalInput)(void*,const AInputEvent*)=nullptr;
int (*originalTick)(void*)=nullptr;
int (*originalMapper)(void*,void*)=nullptr;
void (*feed)(char,char,short,short,short,short)=nullptr;
void (*feedRelative)(char,char,short,short)=nullptr;
bool (*getGrabbed)(void*)=nullptr;
bool (*isInGame)(void*)=nullptr;
int (*getMode)(void*)=nullptr;
int (*getWidth)(void*)=nullptr;
int (*getHeight)(void*)=nullptr;
void* (*getScreen)(void*)=nullptr;
void* (*getHoloInput)(void*)=nullptr;
void (*enqueuePulse)(void*,short)=nullptr;
void (*enqueueDirection)(void*,int,float,float)=nullptr;

void Emit(const Action& action) {
    switch(action.kind) {
        case Action::Position: feed(0,0,action.x,action.y,0,0);break;
        case Action::Relative: feedRelative(0,0,action.x,action.y);break;
        case Action::Button: feedRelative(char(action.button),char(action.state),0,0);break;
        case Action::Scroll: wheel.Add(action.scrollX,action.scrollY);break;
    }
}
void ResetIfRequested() {
    const unsigned generation=resetGeneration.load();
    if(generation==appliedReset) return;
    mouse.Reset(Emit,connected.load());wheel.Reset();appliedReset=generation;
}
bool AlternateTouch() {
    void* holo=client?getHoloInput(client):nullptr;
    if(!holo) return false;
    auto vt=*static_cast<void***>(holo);
    // HolographicPlatform::useAlternateTouchInput, verified primary vtable +96.
    return reinterpret_cast<bool(*)(void*)>(vt[24])(holo);
}
void Refresh() {
    ResetIfRequested();
    bool grab=false;
    if(client) {
        void* screen=getScreen(client);
        if(screen!=lastScreen) {
            mouse.Reset(Emit);wheel.Reset();lastScreen=screen;
        }
    }
    if(client && focused.load() && connected.load() && !AlternateTouch()) {
        width=std::max(1,getWidth(client));height=std::max(1,getHeight(client));
        grab=isInGame(client) && getGrabbed(client) && getMode(client)==1;
    }
    if(!grab) captureReleased.store(false);
    gameplay=grab && !captureReleased.load();
    if(mouse.Context(gameplay,Emit) && !gameplay) wheel.Reset();
    captureWanted.store(gameplay);
}
void MotionEvent(const AInputEvent* event) {
    if(!focused.load()) {ResetIfRequested();return;}
    connected.store(true);
    const int action=AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK;
    if(action==AMOTION_EVENT_ACTION_DOWN || action==AMOTION_EVENT_ACTION_BUTTON_PRESS)
        captureReleased.store(false);
    Refresh();
    Motion motion;
    motion.action=action;
    motion.buttons=AMotionEvent_getButtonState(event)&7;
    if(action==AMOTION_EVENT_ACTION_DOWN && !motion.buttons) motion.buttons=1;
    motion.relative=IsRelativeSource(AInputEvent_getSource(event));
    const float relativeX=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RELATIVE_X,0);
    const float relativeY=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RELATIVE_Y,0);
    const bool relativeAxes=gameplay && !motion.relative && (relativeX!=0 || relativeY!=0);
    if(relativeAxes) motion.relative=true;
    const std::size_t history=AMotionEvent_getHistorySize(event);
    if(action==AMOTION_EVENT_ACTION_MOVE || action==AMOTION_EVENT_ACTION_HOVER_MOVE) {
        // Relative samples are deltas, so historical samples must also be fed.
        for(std::size_t i=0;i<history;++i) {
            motion.x=relativeAxes?AMotionEvent_getHistoricalAxisValue(event,AMOTION_EVENT_AXIS_RELATIVE_X,0,i):AMotionEvent_getHistoricalX(event,0,i);
            motion.y=relativeAxes?AMotionEvent_getHistoricalAxisValue(event,AMOTION_EVENT_AXIS_RELATIVE_Y,0,i):AMotionEvent_getHistoricalY(event,0,i);
            mouse.Process(motion,gameplay,width,height,Emit);
        }
    }
    motion.x=relativeAxes?relativeX:AMotionEvent_getX(event,0);
    motion.y=relativeAxes?relativeY:AMotionEvent_getY(event,0);
    if(action==AMOTION_EVENT_ACTION_SCROLL) {
        motion.scrollX=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_HSCROLL,0);
        motion.scrollY=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_VSCROLL,0);
    }
    mouse.Process(motion,gameplay,width,height,Emit);
    if(action==AMOTION_EVENT_ACTION_CANCEL) wheel.Reset();
}
int Input(void* app,const AInputEvent* event) {
    const int source=AInputEvent_getSource(event);
    const int type=AInputEvent_getType(event);
    if(available.load() && IsMouseSource(source) && !AlternateTouch()) {
        if(type==AINPUT_EVENT_TYPE_MOTION) {
            MotionEvent(event);
            // NativeActivity forwards unhandled events to Java. Consume here
            // before the original callback can put them in Multitouch as well.
            return 1;
        }
        if(type==AINPUT_EVENT_TYPE_KEY && AKeyEvent_getKeyCode(event)==AKEYCODE_BACK) {
            // Older Android mouse drivers may report the secondary button as
            // BACK. Restrict this compatibility path to mouse-source events.
            if(focused.load() && AKeyEvent_getRepeatCount(event)==0) {
                Refresh();
                Motion motion;motion.action=11;motion.relative=true;
                motion.buttons=mouse.PhysicalButtons();
                if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_DOWN) motion.buttons|=2;
                else motion.buttons&=~2;
                mouse.Process(motion,gameplay,width,height,Emit);
            }
            return 1;
        }
    }
    if(available.load() && type==AINPUT_EVENT_TYPE_MOTION && (source&AINPUT_SOURCE_TOUCHSCREEN)==AINPUT_SOURCE_TOUCHSCREEN) {
        const int action=AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK;
        if(action==AMOTION_EVENT_ACTION_DOWN) {
            mouse.Reset(Emit);wheel.Reset();
        }
    }
    return originalInput(app,event);
}
int Tick(void* self) {
    client=self;Refresh();
    const int result=originalTick(self);
    // Screen transitions and input-mode changes happen inside tickInput.
    Refresh();return result;
}
int Mapper(void* self,void* queue) {
    int used=originalMapper(self,queue);
    float x=0,y=0;
    if(wheel.Direction(x,y)) {enqueueDirection(queue,2,x,y);used=1;}
    const int step=wheel.NextStep();
    if(step) {
        // The shipped mapper treats wheel bytes as unsigned and does not read
        // its right-scroll vector. Dispatch the actual current bindings in both
        // directions, preserving UI scrolling and fractional wheel movement.
        const auto& bindings=engine::At<std::vector<short>>(self,step>0?40:52);
        for(short binding:bindings) enqueuePulse(queue,binding);
        used=1;
    }
    return used;
}
const engine::Hook hooks[]={
    {"Android input callback",0x3207cc,"2de9f0432ded028b",reinterpret_cast<void*>(Input),reinterpret_cast<void**>(&originalInput),true},
    {"_ZN15MinecraftClient9tickInputEv",0x3442fc,"10b504460123d0f8",reinterpret_cast<void*>(Tick),reinterpret_cast<void**>(&originalTick)},
    {"_ZN11MouseMapper4tickER15InputEventQueue",0x2c35e4,"2de9f04782b00746",reinterpret_cast<void*>(Mapper),reinterpret_cast<void**>(&originalMapper)},
};
}

void InstallVerified(void* library) {
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(library,name));ok=ok && target
    S("_ZN5Mouse4feedEccssss",feed);
    S("_ZN5Mouse12feedRelativeEccss",feedRelative);
    S("_ZNK15MinecraftClient15getMouseGrabbedEv",getGrabbed);
    S("_ZN15MinecraftClient8isInGameEv",isInGame);
    S("_ZN15MinecraftClient19getCurrentInputModeEv",getMode);
    S("_ZNK15MinecraftClient8getWidthEv",getWidth);
    S("_ZNK15MinecraftClient9getHeightEv",getHeight);
    S("_ZN15MinecraftClient9getScreenEv",getScreen);
    S("_ZNK15MinecraftClient12getHoloInputEv",getHoloInput);
    S("_ZN15InputEventQueue28enqueueButtonPressAndReleaseEs",enqueuePulse);
    S("_ZN15InputEventQueue16enqueueDirectionE11DirectionIdff",enqueueDirection);
#undef S
    if(!ok || !engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0]))) return;
    available.store(true);
    __android_log_print(ANDROID_LOG_INFO,"SunchMouse","Native mouse routing and input hooks installed");
}

int State() {return (available.load()?1:0)|(captureWanted.load()?2:0);}
void WindowState(bool focus,bool mousePresent) {
    const bool oldFocus=focused.exchange(focus),oldMouse=connected.exchange(mousePresent);
    if(oldFocus!=focus || oldMouse!=mousePresent) resetGeneration.fetch_add(1);
    if(!focus || !mousePresent) captureWanted.store(false);
}
void CaptureLost() {
    if(captureWanted.load() && focused.load()) captureReleased.store(true);
    captureWanted.store(false);resetGeneration.fetch_add(1);
}
}}

extern "C" JNIEXPORT jint JNICALL Java_com_mcpe_sunch_client0143_MouseInput_nativeState(JNIEnv*,jclass) {
    return features::mouseinput::State();
}
extern "C" JNIEXPORT void JNICALL Java_com_mcpe_sunch_client0143_MouseInput_nativeWindowState(
        JNIEnv*,jclass,jboolean focused,jboolean connected) {
    features::mouseinput::WindowState(focused,connected);
}
extern "C" JNIEXPORT void JNICALL Java_com_mcpe_sunch_client0143_MouseInput_nativeCaptureLost(JNIEnv*,jclass) {
    features::mouseinput::CaptureLost();
}
