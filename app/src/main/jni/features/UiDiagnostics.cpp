#include "UiDiagnostics.h"
#include "ErrorReports.h"
#include "engine/Engine.h"
#include <android/log.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace features { namespace uidiagnostics {
namespace {
using Value=const void*;
using Loader=std::function<std::string(const std::string&)>;
static_assert(sizeof(Loader)==16 && sizeof(std::shared_ptr<void>)==8,"UI callback ABI");
int (*jsonType)(Value)=nullptr;
unsigned (*jsonSize)(Value)=nullptr;
Value (*jsonMember)(Value,const std::string&)=nullptr;
Value (*jsonIndex)(Value,unsigned)=nullptr;
std::string (*jsonString)(Value,const std::string&)=nullptr;
double (*jsonDouble)(Value,double)=nullptr;
std::vector<std::string> (*jsonNames)(Value)=nullptr;
std::string (*jsonDump)(Value)=nullptr;
void (*originalLoad)(void*,const std::string&,Loader)=nullptr;
bool (*originalParse)(void*,const std::string&,void*,bool)=nullptr;
Value (*originalFind)(Value,const std::string&,const std::string&)=nullptr;
Value (*originalValue)(Value,const std::string&)=nullptr;
std::shared_ptr<void> (*originalCreate)(void*,Value)=nullptr;
int (*originalResolveNames)(void*,const std::shared_ptr<void>&)=nullptr;
std::shared_ptr<void> (*originalFindControl)(std::shared_ptr<void>&,std::string)=nullptr;
uintptr_t imageBase=0;

struct Origin {std::string file,path;};
struct Definition {Origin origin;std::string parent;std::set<std::string> fields;};
std::recursive_mutex metadataMutex;
std::unordered_map<std::string,Definition> definitions;
std::unordered_map<Value,Origin> origins;
std::set<std::string> emitted;
struct Loading {std::string file;};
thread_local Loading* loading=nullptr;
struct Control {Value def;Origin origin;std::string name;std::set<std::string> queried;};
thread_local Control* control=nullptr;
struct ControlLocation {Origin origin;std::string name;};
std::unordered_map<Value,ControlLocation> controlLocations;
thread_local ControlLocation* resolving=nullptr;
thread_local bool inspecting=false;

template<class T> struct Scope {
    T& slot;T previous;
    Scope(T& target,T value):slot(target),previous(target) {slot=value;}
    ~Scope() {slot=previous;}
};
std::string Clip(const std::string& value) {return value.size()>2048?value.substr(0,2048)+" [truncated]":value;}
int Type(Value value) {return value?jsonType(value):0;}
bool Numeric(Value value) {const int type=Type(value);return type>=1 && type<=3;}
std::string Text(Value value) {return Type(value)==4?jsonString(value,std::string()):std::string();}
std::string DefKey(const std::string& space,const std::string& name) {return space+"\n"+name;}
std::string RefKey(const std::string& space,const std::string& reference) {
    const auto dot=reference.find('.');
    return dot==std::string::npos?DefKey(space,reference):DefKey(reference.substr(0,dot),reference.substr(dot+1));
}
Origin Known(Value value) {
    std::lock_guard<std::recursive_mutex> lock(metadataMutex);
    const auto it=origins.find(value);
    return it==origins.end()?Origin{}:it->second;
}
Origin Locate(Value def,const std::string& property,Value value) {
    Origin found=Known(value);
    if(!found.file.empty()) return found;
    // UIResolvedDef: namespace +4, name +8, base definition +12, local override +16.
    if(def) {
        for(unsigned offset:{16u,12u}) {
            Value object=engine::At<Value>(const_cast<void*>(def),offset);
            found=Known(object);
            if(!found.file.empty() && (property.empty() || (Type(object)==7 && Type(jsonMember(object,property))!=0))) {
                if(!property.empty()) found.path+="/"+property;
                return found;
            }
        }
    }
    if(control) {found=control->origin;if(!property.empty()) found.path+="/"+property;}
    if(found.file.empty() && loading) found.file=loading->file;
    return found;
}
void Error(const Origin& origin,const std::string& function,const std::string& property,
           const std::string& expected,Value actual,const std::string& note=std::string()) {
    const std::string rendered=actual?Clip(jsonDump(actual)):"[missing]";
    std::string details="Control: ";details+=resolving?resolving->name:control?control->name:"[definition loading]";
    details+="\nJSON path: "+origin.path+"\nProperty: "+property+"\nExpected: "+expected+"\nActual: "+rendered;
    if(!note.empty()) details+="\n"+note;
    // Keep repeat reports bounded, including alternating errors across frames.
    const std::string key=origin.file+function+details;
    {
        std::lock_guard<std::recursive_mutex> lock(metadataMutex);
        if(emitted.count(key) || emitted.size()>=256) return;
        emitted.insert(key);
    }
    errorreports::RecordUiError(origin.file,function,details);
}
void IndexTree(Value value,const Origin& origin,unsigned depth=0) {
    if(!value || depth>48 || origins.size()>=50000 || origins.count(value)) return;
    origins.emplace(value,origin);
    if(Type(value)==7) {
        for(const auto& name:jsonNames(value)) IndexTree(jsonMember(value,name),Origin{origin.file,origin.path+"/"+name},depth+1);
    } else if(Type(value)==6) {
        for(unsigned i=0;i<jsonSize(value);++i) IndexTree(jsonIndex(value,i),Origin{origin.file,origin.path+"/"+engine::Number(i)},depth+1);
    }
}
void Remember(Value root,const std::string& file) {
    const std::string function="UIDefRepository::_collectAllDefEntries / Json::Reader::parse [RVA 0x351ee0]";
    if(Type(root)!=7) {Error({file,"/"},function,"root","object",root);return;}
    const Value list=jsonMember(root,"ui_defs");
    if(Type(list)!=0) {
        if(Type(list)!=6) Error({file,"/ui_defs"},function,"ui_defs","array of resource filenames",list);
        else for(unsigned i=0;i<jsonSize(list);++i)
            if(Type(jsonIndex(list,i))!=4) Error({file,"/ui_defs/"+engine::Number(i)},function,"ui_defs","string filename",jsonIndex(list,i));
        return;
    }
    const Value namespaceValue=jsonMember(root,"namespace");
    if(Type(namespaceValue)!=0 && Type(namespaceValue)!=4)
        Error({file,"/namespace"},function,"namespace","string",namespaceValue);
    std::string space=Type(namespaceValue)==0?"_root":Text(namespaceValue);
    std::lock_guard<std::recursive_mutex> lock(metadataMutex);
    for(const auto& name:jsonNames(root)) {
        if(Type(jsonMember(root,name))!=7) continue;
        const auto at=name.find('@');
        const std::string local=name.substr(0,at);
        const std::string parent=at==std::string::npos?std::string():name.substr(at+1);
        Value object=jsonMember(root,name);
        std::set<std::string> fields;
        // _resolveReferences skips null overrides while merging ancestors.
        for(const auto& field:jsonNames(object)) if(Type(jsonMember(object,field))!=0) fields.insert(field);
        definitions[DefKey(space,local)]={{file,"/"+name},parent.empty()?std::string():RefKey(space,parent),std::move(fields)};
    }
}
void CheckInheritance(Value repository,const std::string& requested) {
    std::lock_guard<std::recursive_mutex> lock(metadataMutex);
    std::set<std::string> seen;
    std::string current=requested;
    std::string requestedName=requested;std::replace(requestedName.begin(),requestedName.end(),'\n','.');
    while(!current.empty()) {
        const auto entry=definitions.find(current);
        if(entry==definitions.end()) break;
        const auto& definition=entry->second;
        if(!seen.insert(current).second) {
            Error(definition.origin,"UIResolvedDef::create / inheritance resolution [RVA 0x3364a4]","inheritance",
                "acyclic definition references",nullptr,"Cyclic inheritance while creating: "+requestedName);
            break;
        }
        // Variable-containing references need the runtime variable stack.
        if(definition.parent.empty() || definition.parent.find('$')!=std::string::npos) break;
        if(!definitions.count(definition.parent)) {
            const auto separator=definition.parent.find('\n');
            // Code may register definitions outside the tracked asset loader.
            // Metadata absence alone does not prove a runtime lookup failed.
            if(separator!=std::string::npos && Type(originalFind(repository,
                    definition.parent.substr(0,separator),definition.parent.substr(separator+1)))!=0) break;
            std::string target=definition.parent;std::replace(target.begin(),target.end(),'\n','.');
            Error(definition.origin,"UIResolvedDef::create / inheritance resolution [RVA 0x3364a4]","inheritance",
                "existing namespace.definition",nullptr,"Unresolved reference: "+target+"\nRequested definition: "+requestedName);
            break;
        }
        current=definition.parent;
    }
}
void Load(void* self,const std::string& list,Loader loader) {
    {
        std::lock_guard<std::recursive_mutex> lock(metadataMutex);
        definitions.clear();origins.clear();controlLocations.clear();emitted.clear();
    }
    Loading current{list};Scope<Loading*> scope(loading,&current);
    Loader tracked=[&](const std::string& filename) {
        current.file=filename;
        return loader(filename);
    };
    originalLoad(self,list,std::move(tracked));
}
bool Parse(void* reader,const std::string& text,void* root,bool comments) {
    const bool success=originalParse(reader,text,root,comments);
    if(success && loading) {Scope<bool> guard(inspecting,true);Remember(root,loading->file);}
    return success;
}
Value Find(Value self,const std::string& space,const std::string& name) {
    const uintptr_t caller=(reinterpret_cast<uintptr_t>(__builtin_return_address(0))&~uintptr_t(1))-imageBase;
    Value result=originalFind(self,space,name);
    if(inspecting) return result;
    Scope<bool> guard(inspecting,true);
    const bool creating=caller>=0x3364a4 && caller<0x336874;
    // Loading indexes every template, including optional platform screens.
    // Only follow inheritance once the engine actually creates a definition.
    if(creating && Type(result)!=0) CheckInheritance(self,DefKey(space,name));
    {
        std::lock_guard<std::recursive_mutex> lock(metadataMutex);
        const auto it=definitions.find(DefKey(space,name));
        if(Type(result)!=0 && it!=definitions.end() && !origins.count(result)) {
            // The repository flattens inheritance. Preserve the file that
            // originally declared each property rather than blaming the child.
            origins.emplace(result,it->second.origin);
            for(const auto& property:jsonNames(result)) {
                auto owner=it;std::set<std::string> visited;
                while(!owner->second.fields.count(property) && !owner->second.parent.empty()) {
                    if(!visited.insert(owner->first).second) break;
                    const auto parent=definitions.find(owner->second.parent);
                    if(parent==definitions.end()) break;
                    owner=parent;
                }
                IndexTree(jsonMember(result,property),Origin{owner->second.origin.file,owner->second.origin.path+"/"+property});
            }
        }
    }
    // Other callers may legitimately probe for an optional definition.
    if(Type(result)==0 && creating) {
        Origin origin=control?control->origin:Origin{};
        Error(origin,"UIResolvedDef::create / UIDefRepository::findDef [RVA 0x2e4c2c]","reference",
            "existing namespace.definition",nullptr,"Unresolved reference: "+space+"."+name);
    }
    return result;
}

enum class Kind {Boolean,Integer,Number,String,Enum,Color,Vector,Grid,Slice,Layout,Strings};
struct Rule {uintptr_t begin,end;Kind kind;const char* function;const char* choices;};
// Exact getter ranges and enum strings recovered from the verified engine.
const Rule rules[]={
    {0x2e530c,0x2e5404,Kind::Enum,"getDefType","button|carousel_label|custom|factory|edit_box|grid|grid_item|image|input_panel|label|panel|screen|scrollbar|scrollbar_box|tab|toggle"},
    {0x2e5404,0x2e5506,Kind::Enum,"getAsAnchorPoint","top_left|top_middle|top_right|right_middle|bottom_right|bottom_middle|bottom_left|left_middle|center"},
    {0x2e5530,0x2e5622,Kind::Enum,"getAsAnimationType","alpha|clip|color|flip_book|offset|size|uv|wait"},
    {0x2e5648,0x2e570a,Kind::Enum,"getAsClipDirection","left|right|up|down|center"},
    {0x2e5724,0x2e57b8,Kind::Enum,"getAsTextureSource","package|cache"},
    {0x2e57c8,0x2e5802,Kind::Boolean,"getAsBool",nullptr},
    {0x2e5804,0x2e58c4,Kind::Enum,"getAsFontType","default|rune"},
    {0x2e58d0,0x2e5998,Kind::String,"getAsButtonId",nullptr},
    {0x2e59a0,0x2e59da,Kind::Integer,"getAsInt",nullptr},
    {0x2e59dc,0x2e5a16,Kind::Number,"getAsFloat",nullptr},
    {0x2e5a18,0x2e5a76,Kind::String,"getAsString",nullptr},
    {0x2e5a7c,0x2e5b2e,Kind::Enum,"getAsDraggable","not_draggable|horizontal|vertical|both"},
    {0x2e5b44,0x2e5bd6,Kind::Enum,"getAsFontSize","small|medium|large"},
    {0x2e5be4,0x2e5c86,Kind::Enum,"getAsGridRescalingType","horizontal|vertical|none"},
    {0x2e5c98,0x2e5d4a,Kind::Enum,"getAsBindingCondition","none|once|visible|visibility_changed"},
    {0x2e5d60,0x2e5e12,Kind::Enum,"getAsBindingType","global|collection|collection_details|view"},
    {0x2e5e28,0x2e5eda,Kind::Enum,"getAsToggleManagerBehavior","clear|select|gather|default"},
    {0x317eac,0x317f8c,Kind::Color,"getAsColor",nullptr},
    {0x318098,0x318132,Kind::Vector,"getAsSize",nullptr},
    {0x318134,0x3181d0,Kind::Vector,"getAsClipOffset",nullptr},
    {0x3181d0,0x31826c,Kind::Vector,"getAsUV",nullptr},
    {0x318368,0x318404,Kind::Vector,"getAsUVSize",nullptr},
    {0x318404,0x31850e,Kind::Slice,"getAsSliceSize",nullptr},
    {0x318510,0x3185b0,Kind::Grid,"getAsGridSize",nullptr},
    {0x318ad8,0x318c6c,Kind::Layout,"getAsLayoutSize",nullptr},
    {0x3228c8,0x322ac6,Kind::Strings,"getAsStringVector",nullptr},
};
bool Choice(const std::string& value,const char* choices) {
    const std::string list=std::string("|")+choices+"|";
    return list.find("|"+value+"|")!=std::string::npos;
}
bool LayoutExpression(std::string text) {
    for(char& c:text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char* at=text.c_str();bool term=false;
    while(*at) {
        while(std::isspace(static_cast<unsigned char>(*at))) ++at;
        if(!*at) return term;
        if(term && *at!='+' && *at!='-') return false;
        if(*at=='+' || *at=='-') ++at;
        while(std::isspace(static_cast<unsigned char>(*at))) ++at;
        char* end=nullptr;const double number=std::strtod(at,&end);
        if(end==at || !std::isfinite(number)) return false;
        at=end;
        while(std::isspace(static_cast<unsigned char>(*at))) ++at;
        if(*at=='%') {++at;if(*at=='x' || *at=='y') ++at;}
        else if(at[0]=='p' && at[1]=='x') at+=2;
        term=true;
    }
    return term;
}
std::string Validate(const Rule& rule,Value value) {
    const int type=Type(value);
    const std::string text=Text(value);
    // #name values are resolved by the binding system, not static getters.
    if(type==4 && !text.empty() && text[0]=='#' && rule.kind!=Kind::Enum) return {};
    switch(rule.kind) {
        case Kind::Boolean:return type==5?"":"boolean";
        case Kind::Integer:return type==1?"":"integer (this getter does not accept floating-point JSON values)";
        case Kind::Number:return Numeric(value) && std::isfinite(jsonDouble(value,0.0))?"":"finite number";
        case Kind::String:return type==4?"":"string";
        case Kind::Enum:return type==4 && Choice(text,rule.choices)?"":std::string("one of: ")+rule.choices;
        case Kind::Slice:if(Numeric(value) && std::isfinite(jsonDouble(value,0.0))) return {};break;
        default:break;
    }
    if(type!=6) return rule.kind==Kind::Slice?"number or array of four numbers":"array";
    const unsigned count=jsonSize(value);
    if(rule.kind==Kind::Color && count!=3 && count!=4) return "array of three or four numeric color components";
    if((rule.kind==Kind::Vector || rule.kind==Kind::Grid || rule.kind==Kind::Layout) && count!=2) return "array with two components";
    if(rule.kind==Kind::Slice && count!=4) return "number or array of four numbers";
    for(unsigned i=0;i<count;++i) {
        Value item=jsonIndex(value,i);
        if(rule.kind==Kind::Strings) {if(Type(item)!=4) return "array of strings";}
        else if(rule.kind==Kind::Layout && Type(item)==4) {
            if(!LayoutExpression(Text(item))) return "layout expression such as 100%, 100%x - 4px, or a numeric coordinate";
        } else if(!Numeric(item) || !std::isfinite(jsonDouble(item,0.0))) return "array of finite numeric components";
        else if(rule.kind==Kind::Grid && std::floor(jsonDouble(item,0.0))!=jsonDouble(item,0.0)) return "array of integer grid dimensions";
    }
    return {};
}
Value GetValue(Value self,const std::string& property) {
    const uintptr_t caller=(reinterpret_cast<uintptr_t>(__builtin_return_address(0))&~uintptr_t(1))-imageBase;
    if(!inspecting) {
        // Release Json::Value::operator[] has no object-type assertion. Capture
        // malformed child definitions before that code dereferences them.
        Scope<bool> guard(inspecting,true);
        for(unsigned offset:{16u,12u}) {
            Value object=engine::At<Value>(const_cast<void*>(self),offset);
            if(Type(object)!=0 && Type(object)!=7) Error(Locate(self,std::string(),object),
                "UIResolvedDef::getValue [RVA 0x2e5294]",property,"control definition object",object);
        }
    }
    Value value=originalValue(self,property);
    if(inspecting) return value;
    Scope<bool> guard(inspecting,true);
    if(control && control->def==self) control->queried.insert(property);
    const Rule* rule=nullptr;
    for(const auto& candidate:rules) if(caller>=candidate.begin && caller<candidate.end) {rule=&candidate;break;}
    const int type=Type(value);
    // Missing optional parameters use engine defaults. A control type is required.
    if(type==0 && (!rule || rule->begin!=0x2e530c)) return value;
    std::string expected;
    const std::string text=Text(value);
    // The constructor reads ignored before pushing this definition's variables.
    if(property=="ignored" && type==4 && !text.empty() && text[0]=='$') return value;
    if(type==4 && !text.empty() && text[0]=='$' && property!="ignored" && !property.empty() && property[0]!='$' && rule)
        expected="defined UI variable (including |default fallback)";
    else if(rule) expected=Validate(*rule,value);
    else if(property=="controls" || property=="bindings" || property=="variables") {
        if(property=="variables" && type==7) return value;
        if(type!=6) expected=property=="variables"?"object or array of variable sets":"array of objects";
        else for(unsigned i=0;i<jsonSize(value);++i) {
            Value entry=jsonIndex(value,i);
            if(Type(entry)!=7 || (property=="controls" && jsonSize(entry)!=1)) {expected=property=="controls"?"array of single-name control objects":"array of objects";break;}
            if(property=="controls") {
                const auto names=jsonNames(entry);
                if(Type(jsonMember(entry,names[0]))!=7) {expected="each named control must contain an object";break;}
            }
        }
    }
    if(!expected.empty()) {
        char location[128];::snprintf(location,sizeof(location),"UIResolvedDef::%s [caller RVA 0x%lx]",rule?rule->function:"getValue",static_cast<unsigned long>(caller));
        Error(Locate(self,property,value),location,property,expected,value);
    }
    return value;
}
bool OneEdit(const std::string& left,const std::string& right) {
    if(left==right || left.size()+1<right.size() || right.size()+1<left.size()) return false;
    std::size_t i=0,j=0;unsigned edits=0;
    while(i<left.size() && j<right.size()) {
        if(left[i]==right[j]) {++i;++j;continue;}
        if(++edits>1) return false;
        if(left.size()<=right.size()) ++j;
        if(left.size()>=right.size()) ++i;
    }
    return edits+(i<left.size() || j<right.size()?1:0)==1;
}
void CheckSpelling(const Control& current) {
    // Diagnose only a unique, close match to a property actually read for this
    // control. Arbitrary custom fields and $variables have no global schema.
    Value local=engine::At<Value>(const_cast<void*>(current.def),16);
    if(Type(local)!=7) return;
    for(const auto& name:jsonNames(local)) {
        if(name.size()<4 || name[0]=='$' || name[0]=='#' || current.queried.count(name)) continue;
        std::string suggestion;
        for(const auto& read:current.queried) if(OneEdit(name,read)) {
            if(!suggestion.empty()) {suggestion.clear();break;}
            suggestion=read;
        }
        if(!suggestion.empty()) Error(Locate(current.def,name,jsonMember(local,name)),
            "UIControlFactory::_createFromResolvedDef [RVA 0x3400a0]",name,"recognized property",
            jsonMember(local,name),"Possible misspelling: "+suggestion+". This property was not read while constructing this control; custom properties may be intentional.");
    }
}
std::shared_ptr<void> Create(void* self,Value def) {
    Control current{def,Locate(def,std::string(),nullptr),std::string(),{}};
    current.name=engine::At<std::string>(const_cast<void*>(def),4)+"."+engine::At<std::string>(const_cast<void*>(def),8);
    Scope<Control*> scope(control,&current);
    try {
        auto result=originalCreate(self,def);
        Scope<bool> guard(inspecting,true);CheckSpelling(current);
        if(result) {
            std::lock_guard<std::recursive_mutex> lock(metadataMutex);
            if(controlLocations.size()<50000) controlLocations[result.get()]={current.origin,current.name};
        }
        return result;
    }
    catch(const std::exception& error) {
        Scope<bool> guard(inspecting,true);
        Error(current.origin,"UIControlFactory::_createFromResolvedDef [RVA 0x3400a0]","control construction",
            "valid UI control parameters",nullptr,std::string("Exception: ")+error.what());
        throw;
    }
}
int ResolveNames(void* self,const std::shared_ptr<void>& root) {
    ControlLocation current;
    {
        std::lock_guard<std::recursive_mutex> lock(metadataMutex);
        const auto it=controlLocations.find(self);
        if(it!=controlLocations.end()) current=it->second;
    }
    Scope<ControlLocation*> scope(resolving,&current);
    return originalResolveNames(self,root);
}
std::shared_ptr<void> FindControl(std::shared_ptr<void>& root,std::string name) {
    const uintptr_t caller=(reinterpret_cast<uintptr_t>(__builtin_return_address(0))&~uintptr_t(1))-imageBase;
    auto result=originalFindControl(root,name);
    if(!result && resolving && !name.empty() && caller>=0x326274 && caller<0x3263ec) {
        Scope<bool> guard(inspecting,true);
        Error(resolving->origin,"UIControl::_resolveControlNames / getControlBreadthFirst [RVA 0x326274]",
            "control reference","existing control in the configured resolution scope",nullptr,"Unresolved control name: "+name);
    }
    return result;
}
const engine::Hook hooks[]={
    {"_ZN15UIDefRepository12loadDefsListERKSsSt8functionIFSsS1_EE",0x352278,"2de9f04f8fb07f4e",reinterpret_cast<void*>(Load),reinterpret_cast<void**>(&originalLoad)},
    {"_ZN4Json6Reader5parseERKSsRNS_5ValueEb",0x3519f8,"70b5044682b05030",reinterpret_cast<void*>(Parse),reinterpret_cast<void**>(&originalParse)},
    {"_ZNK15UIDefRepository7findDefERKSsS1_",0x2e4c2c,"2de9f84f04460868",reinterpret_cast<void*>(Find),reinterpret_cast<void**>(&originalFind)},
    {"_ZNK13UIResolvedDef8getValueERKSs",0x2e5294,"037870b504460d46",reinterpret_cast<void*>(GetValue),reinterpret_cast<void**>(&originalValue)},
    {"_ZN16UIControlFactory22_createFromResolvedDefERK13UIResolvedDef",0x3400a0,"2de9f04fa5b00023",reinterpret_cast<void*>(Create),reinterpret_cast<void**>(&originalCreate)},
    {"_ZN9UIControl20_resolveControlNamesERKSt10shared_ptrIS_E",0x326274,"2de9f04f0746d0f8",reinterpret_cast<void*>(ResolveNames),reinterpret_cast<void**>(&originalResolveNames)},
    {"_Z22getControlBreadthFirstRSt10shared_ptrI9UIControlESs",0x325ef4,"2de9f04f97b00024",reinterpret_cast<void*>(FindControl),reinterpret_cast<void**>(&originalFindControl)},
};
}
void InstallVerified(void* library) {
    bool ok=true;
#define S(name,target) target=reinterpret_cast<decltype(target)>(dlsym(library,name));ok=ok && target
    S("_ZNK4Json5Value4typeEv",jsonType);
    S("_ZNK4Json5Value4sizeEv",jsonSize);
    S("_ZNK4Json5ValueixERKSs",jsonMember);
    S("_ZNK4Json5ValueixEj",jsonIndex);
    S("_ZNK4Json5Value8asStringERKSs",jsonString);
    S("_ZNK4Json5Value8asDoubleEd",jsonDouble);
    S("_ZNK4Json5Value14getMemberNamesEv",jsonNames);
    S("_ZNK4Json5Value14toStyledStringEv",jsonDump);
#undef S
    Dl_info image{};
    void* anchor=dlsym(library,"_ZNK13UIResolvedDef8getValueERKSs");
    if(!ok || !anchor || !dladdr(anchor,&image)) return;
    imageBase=reinterpret_cast<uintptr_t>(image.dli_fbase);
    if(engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0])))
        __android_log_print(ANDROID_LOG_INFO,"SunchErrors","Verified JSON UI parameter diagnostics installed");
}
}}
