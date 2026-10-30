#pragma once
#include <algorithm>
#include <cmath>

namespace features { namespace mouseinput {
// Android source/action values are kept here so the conversion can be tested
// on a host without Android headers or a running game.
inline bool IsMouseSource(int source) {
    return (source&0x2002)==0x2002 || (source&0x20004)==0x20004;
}
inline bool IsRelativeSource(int source) {return (source&0x20004)==0x20004;}
inline float FiniteBound(float value,float bound) {
    return std::isfinite(value)?std::max(-bound,std::min(bound,value)):0.f;
}
struct Motion {
    int action=7,buttons=0;
    bool relative=false;
    float x=0,y=0,scrollX=0,scrollY=0;
};
struct Action {
    enum Kind {Position,Relative,Button,Scroll} kind;
    int button=0,state=0;
    short x=0,y=0;
    float scrollX=0,scrollY=0;
};

class MouseState {
    int buttons=0,physical=0,suppressed=0;
    bool contextKnown=false,playing=false,absoluteKnown=false,lastRelative=false;
    float lastX=0,lastY=0,cursorX=0,cursorY=0,fractionX=0,fractionY=0;
    static short Coordinate(float value,int extent) {
        return short(std::max(0.f,std::min(float(std::min(std::max(extent-1,0),32767)),FiniteBound(value,32767.f))));
    }
    static short Delta(float value,float& fraction) {
        const float sum=FiniteBound(value,32767.f)+fraction;
        const short whole=short(std::max(-32767.f,std::min(32767.f,sum)));
        fraction=sum-whole;
        if(std::fabs(fraction)>=1.f) fraction=0.f;
        return whole;
    }
public:
    int PhysicalButtons() const {return physical;}
    template<class Sink> void Reset(Sink sink,bool suppressHeld=true) {
        for(int i=0;i<3;++i) if(buttons&(1<<i)) {
            Action action{Action::Button};action.button=i+1;sink(action);
        }
        suppressed=suppressHeld?(suppressed|physical):0;
        buttons=0;physical=0;contextKnown=false;absoluteKnown=false;lastRelative=false;
        fractionX=fractionY=0;
    }
    template<class Sink> bool Context(bool gameplay,Sink sink) {
        if(contextKnown && playing==gameplay) return false;
        // Acquiring capture in the same world must preserve a held mining
        // button. Screen changes are reset separately by the engine adapter.
        if(contextKnown && playing && !gameplay) Reset(sink);
        absoluteKnown=false;lastRelative=false;fractionX=fractionY=0;
        playing=gameplay;contextKnown=true;return true;
    }
    template<class Sink> void Process(const Motion& event,bool gameplay,int width,int height,Sink sink) {
        Context(gameplay,sink);
        if(event.action==3) {Reset(sink,false);return;}
        if(event.action==10) {absoluteKnown=false;return;}
        if(event.action==2 || event.action==7 || event.action==9 || event.action==0 || event.action==1 ||
           event.action==11 || event.action==12 || event.action==8) {
            const bool valid=std::isfinite(event.x) && std::isfinite(event.y);
            if(!gameplay && valid) {
                if(event.relative) {
                    cursorX+=FiniteBound(event.x,32767.f);cursorY+=FiniteBound(event.y,32767.f);
                } else {
                    cursorX=FiniteBound(event.x,32767.f);cursorY=FiniteBound(event.y,32767.f);
                }
                cursorX=Coordinate(cursorX,width);cursorY=Coordinate(cursorY,height);
                Action action{Action::Position};action.x=short(cursorX);action.y=short(cursorY);sink(action);
            } else if(gameplay && valid && (event.action==2 || event.action==7)) {
                float dx=0,dy=0;
                if(event.relative) {dx=event.x;dy=event.y;}
                else if(absoluteKnown && !lastRelative) {dx=event.x-lastX;dy=event.y-lastY;}
                Action action{Action::Relative};
                action.x=Delta(dx,fractionX);action.y=Delta(dy,fractionY);
                if(action.x || action.y) sink(action);
            }
            if(!valid) absoluteKnown=false;
            else if(!event.relative) {lastX=FiniteBound(event.x,32767.f);lastY=FiniteBound(event.y,32767.f);absoluteKnown=true;}
            lastRelative=event.relative;
            physical=event.buttons&7;
            suppressed&=physical;
            const int next=physical&~suppressed;
            for(int i=0;i<3;++i) if((buttons^next)&(1<<i)) {
                Action action{Action::Button};action.button=i+1;action.state=(next>>i)&1;sink(action);
            }
            buttons=next;
            if(event.action==8) {
                Action action{Action::Scroll};
                action.scrollX=FiniteBound(event.scrollX,32.f);action.scrollY=FiniteBound(event.scrollY,32.f);
                if(action.scrollX || action.scrollY) sink(action);
            }
        }
    }
};

class WheelState {
    float horizontal=0,vertical=0,steps=0;
public:
    void Add(float x,float y) {
        horizontal=FiniteBound(horizontal+FiniteBound(x,32),32);
        vertical=FiniteBound(vertical+FiniteBound(y,32),32);
        steps=FiniteBound(steps+FiniteBound(y,32),32);
    }
    bool Direction(float& x,float& y) {
        x=horizontal;y=vertical;horizontal=vertical=0;
        return x!=0 || y!=0;
    }
    // InputHandler deduplicates identical button pulses within one tick.
    // Spread multi-notch hotbar changes over ticks instead of losing notches.
    int NextStep() {
        if(steps>=1) {steps-=1;return 1;}
        if(steps<=-1) {steps+=1;return -1;}
        return 0;
    }
    void Reset() {horizontal=vertical=steps=0;}
};
}}
